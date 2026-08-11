/*
 * nex_bigfloat.c：bigfloat_ty（可调精度二进制浮点数）核心实现。
 *
 * 职责（设计文档 §7）：生命周期与上下文、分类断言、舍入打包
 * （nex_bf_round_pack）、四则运算与 sqrt、比较、分解与合成、
 * 与 bigint / double 互转。十进制字符串 I/O 由 nex_bigfloat_str.c
 * 实现（from_str / to_str / nex_bf_from_decimal / nex_bf_dec_round）。
 *
 * 语义要点（设计文档 §7.1 / §7.3 / §11）：
 *   - 正常值 value = (−1)^sign × mant × 2^exp；nex_bf_round_pack 输出的
 *     正常值尾数恰好 mant_bits 位（最高位恒 1），from_f64 输出 53 位
 *     （double 尾数位长），两者都是唯一的规范化表示；
 *   - 舍入：运算先精确求值，再按 ctx->round 一次舍入（无双重舍入）。
 *     被移出位（removed）与调用方粘位标记（sticky_extra）共同参与
 *     最近偶（ties-to-even）判定，正确性论证见 nex_bf_round_pack；
 *   - 上溢按舍入方向产生 ±∞ 或最大有限值；下溢 flush-to-zero 产生
 *     ±0，零符号 = 精确结果符号（对齐 IEEE 754-2008 §7.5）；
 *   - 加法/减法采用"精确对齐"：两操作数分别左移到公共指数（较小指数）
 *     后整数加减，全程无信息丢失，正确舍入由 round_pack 保证。
 *     设计文档 §7.5 的"指数差过大 → 小的一方退化为粘位"优化仅在主导
 *     操作数位长 ≥ mant_bits 时严格正确（否则粘位量级未知、无法判定
 *     ties），v1 为正确性优先不启用，列为后续性能优化；
 *   - 特殊值传播真值表（NaN / ±∞ / ±0）见各运算注释；
 *   - 除零语义：整数/有理数返回 DIV_ZERO；浮点按 IEEE 754 产生 ±∞
 *     / NaN（§11），bigfloat 层因此不定义 DIV_ZERO 错误码。
 *
 * 规范化不变式（§3.4）：正常值尾数最高位恒 1；±0 / ±∞ / NaN 时
 * mant 与 exp 无意义（置零）。所有公开 API 的输出均满足。
 */

#include "nex/bigfloat/nex_bigfloat_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 校验精度上下文
 * return: ctx 合法返回 true
 * note: mant_bits ≥ 2、1 ≤ exp_bits ≤ 61、round 为合法枚举值
 */
static bool valid_ctx(const bigfloat_ctx_ty *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    if ((ctx->mant_bits < 2U) || (ctx->exp_bits < 1U)
            || (ctx->exp_bits > BIGFLOAT_MAX_EXP_BITS)) {
        return false;
    }
    if ((ctx->round < BIGFLOAT_ROUND_NEAREST_EVEN_E)
            || (ctx->round > BIGFLOAT_ROUND_AWAY_ZERO_E)) {
        return false;
    }
    return true;
}

/*
 * brief: 计算上下文指数范围 [emin, emax]
 * param: ctx  精度上下文（已校验）
 * param: emin 传出最小指数
 * param: emax 传出最大指数
 * note: 范围公式 [−2^(exp_bits−1), 2^(exp_bits−1) − 1]（§7.3）；
 *       exp_bits ≤ 61 保证 2^(exp_bits−1) 与取负均不溢出 int64
 */
static void ctx_exp_range(const bigfloat_ctx_ty *ctx, int64_t *emin,
        int64_t *emax)
{
    const uint64_t half = UINT64_C(1) << (ctx->exp_bits - 1U);
    *emin = -(int64_t)half;
    *emax = (int64_t)(half - 1U);
}

/*
 * brief: bigint 错误码映射为 bigfloat 错误码（设计文档 §3.1）
 * note: 无同名分类的 bigint 错误（INVALID / DIV_ZERO 等）映射为 INVALID
 */
static bigfloat_err_ty map_bigint_err(bigint_err_ty err)
{
    if (err == BIGINT_OK_E) {
        return BIGFLOAT_OK_E;
    }
    if (err == BIGINT_ERR_OOM_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    return BIGFLOAT_ERR_INVALID_E;
}

/*
 * brief: 将 dst 置为指定特殊值（±0 / ±∞ / NaN），释放原有尾数资源
 * param: dst  目标对象
 * param: flag 目标标志
 */
static void set_special(bigfloat_ty *dst, bigfloat_flag_ty flag)
{
    bigfloat_free(dst);
    dst->flag = flag;
    dst->exp = 0;
}

/*
 * brief: 深拷贝 src 的全部字段到 dst
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 *         （dst 不变）
 */
static bigfloat_err_ty copy_value(bigfloat_ty *dst, const bigfloat_ty *src)
{
    const bigint_err_ty berr = bigint_bin_copy(&dst->mant, &src->mant);
    if (berr != BIGINT_OK_E) {
        return map_bigint_err(berr);
    }
    dst->exp = src->exp;
    dst->flag = src->flag;
    return BIGFLOAT_OK_E;
}

/*
 * brief: 检查幅值低位区间 [0, count) 内是否有置位
 * param: val   幅值（非负）
 * param: count 低位个数；count 为 0 恒返回 false
 */
static bool low_bits_nonzero(const bigint_bin_ty *val, size_t count)
{
    for (size_t idx = 0U; idx < count; idx++) {
        if (bigint_bin_bit_test(val, idx)) {
            return true;
        }
    }
    return false;
}

/*
 * brief: 构造最大有限值尾数 2^mant_bits − 1
 * param: dst       输出尾数（已初始化）
 * param: mant_bits 尾数精度（位）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
static bigint_err_ty max_finite_mant(bigint_bin_ty *dst, size_t mant_bits)
{
    bigint_err_ty err = bigint_bin_from_u64(dst, UINT64_C(1));
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin_shl(dst, dst, mant_bits);
    if (err != BIGINT_OK_E) {
        return err;
    }
    bigint_bin_ty one;
    err = bigint_bin_init(&one);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin_from_u64(&one, UINT64_C(1));
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&one);
        return err;
    }
    err = bigint_bin_sub(dst, dst, &one);
    bigint_bin_free(&one);
    return err;
}

/*
 * brief: 将非负 bigint 右移 shift 位并最近偶舍入（IEEE 754 默认）
 * param: out   传出舍入后的 64 位结果（调用方保证结果 ≤ 53 位）
 * param: val   源幅值（非负）
 * param: shift 右移位数
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（out 不变）
 */
static bigint_err_ty shift_round_nearest(uint64_t *out,
        const bigint_bin_ty *val, size_t shift)
{
    bigint_bin_ty tmp;
    bigint_err_ty berr = bigint_bin_init(&tmp);
    if (berr != BIGINT_OK_E) {
        return berr;
    }
    berr = bigint_bin_shr(&tmp, val, shift);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return berr;
    }
    uint64_t trunc = 0U;
    berr = bigint_bin_to_u64(&tmp, &trunc);
    bigint_bin_free(&tmp);
    if (berr != BIGINT_OK_E) {
        return berr;  // 结果超 64 位：调用方保证 ≤ 53 位，此处不会触发
    }
    if (shift == 0U) {
        *out = trunc;
        return BIGINT_OK_E;
    }
    const bool round_bit = bigint_bin_bit_test(val, shift - 1U);
    const bool low_zero = !low_bits_nonzero(val, shift - 1U);
    const bool round_up = round_bit
            && (low_zero ? ((trunc & 1U) != 0U) : true);
    *out = trunc + (round_up ? 1U : 0U);
    return BIGINT_OK_E;
}

/*
 * brief: 比较两个 (幅值, 指数) 对 |m1| × 2^e1 与 |m2| × 2^e2
 * return: lhs < rhs 为负，相等为 0，lhs > rhs 为正
 * note: 不分配大对象；指数差巨大时直接由位长判定。
 *       推导：e1 > e2 时 d = e1 − e2，两边同除 2^e2 → 比较 m1×2^d 与 m2；
 *       e2 > e1 时对称比较 m1 与 m2×2^d。
 */
static int cmp_mag_exp(const bigint_bin_ty *m1, int64_t e1,
        const bigint_bin_ty *m2, int64_t e2)
{
    if (e1 == e2) {
        return bigint_bin_cmp(m1, m2);
    }

    if (e1 > e2) {
        // d = e1 − e2；e2 < 0 且 e1 > INT64_MAX + e2 时差溢出 int64，
        // 此时 d ≥ 2^62 量级，远超任何实际位长 → 直接判定 lhs > rhs
        if ((e2 < 0) && (e1 > INT64_MAX + e2)) {
            return 1;  // m1×2^d ≥ 2^d ≫ |m2|
        }
        const uint64_t d = (uint64_t)(e1 - e2);
        const uint64_t bl2 = (uint64_t)bigint_bin_bit_len(m2);
        if (d >= bl2 + 1U) {
            return 1;  // m1×2^d ≥ 2^(bl1−1+d) ≥ 2^(bl1+bl2) > 2^bl2 > m2
        }
        bigint_bin_ty tmp;
        bigint_err_ty berr = bigint_bin_init(&tmp);
        if (berr != BIGINT_OK_E) {
            return -1;  // OOM 防御：退回"小于"判定
        }
        berr = bigint_bin_shl(&tmp, m1, (size_t)d);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&tmp);
            return -1;
        }
        const int result = bigint_bin_cmp(&tmp, m2);
        bigint_bin_free(&tmp);
        return result;
    }

    // e2 > e1：对称，比较 m1 与 m2×2^d（d = e2 − e1）
    if ((e1 < 0) && (e2 > INT64_MAX + e1)) {
        return -1;  // m2×2^d ≥ 2^d ≫ |m1|
    }
    const uint64_t d = (uint64_t)(e2 - e1);
    const uint64_t bl1 = (uint64_t)bigint_bin_bit_len(m1);
    if (d >= bl1 + 1U) {
        return -1;  // m2×2^d ≥ 2^(bl2−1+d) ≥ 2^(bl1+bl2) > 2^bl1 > m1
    }
    bigint_bin_ty tmp;
    bigint_err_ty berr = bigint_bin_init(&tmp);
    if (berr != BIGINT_OK_E) {
        return 1;  // OOM 防御：退回"大于"判定
    }
    berr = bigint_bin_shl(&tmp, m2, (size_t)d);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return 1;
    }
    const int result = bigint_bin_cmp(m1, &tmp);
    bigint_bin_free(&tmp);
    return result;
}

/*
 * brief: 标志是否为"负"（正常负值 / −0 / −∞；NaN 视为非负）
 * note: 乘法/除法的符号传播需覆盖三个负标志，仅判 NEG_E 会把
 *       −0 / −∞ 操作数误当正号（历史缺陷）
 */
static bool flag_is_negative(bigfloat_flag_ty flag)
{
    return (flag == BIGFLOAT_NEG_E) || (flag == BIGFLOAT_NEG_ZERO_E)
            || (flag == BIGFLOAT_NEG_INF_E);
}

/*
 * brief: 标志的全序秩：−∞ < 负 < ±0 < 正 < +∞
 */
static int flag_rank(bigfloat_flag_ty flag)
{
    switch (flag) {
        case BIGFLOAT_NEG_INF_E:
            return 0;
        case BIGFLOAT_NEG_E:
            return 1;
        case BIGFLOAT_POS_ZERO_E:
        case BIGFLOAT_NEG_ZERO_E:
            return 2;
        case BIGFLOAT_POS_E:
            return 3;
        case BIGFLOAT_POS_INF_E:
            return 4;
        default:
            return 5;  // NaN
    }
}

/*
 * brief: 整数平方根：q = floor(sqrt(x))，余数 rem = x − q²（均非负）
 * param: q   输出平方根（已初始化）
 * param: rem 输出余数（已初始化）
 * param: x   被开方数（非负）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 逐位试商法：每轮消费 x 的两位、产出一位结果——
 *       rem = 4·rem + x 的位 (2i−1, 2i−2)；q = 2·q；试商 t = 2·q + 1，
 *       若 t ≤ rem 则 rem −= t 且 q 最低位置 1。迭代 k = ceil(bit_len/2)
 *       轮（结果位数恰为 k）。复杂度 O(n² / 32)，正确性优先（§7.5）。
 *       注意：若每轮只消费 x 的一位（rem = 2·rem + bit），q 的位数会
 *       膨胀为 n 而非 k——历史缺陷，已修正。
 */
static bigint_err_ty isqrt(bigint_bin_ty *q, bigint_bin_ty *rem,
        const bigint_bin_ty *x)
{
    bigint_bin_ty t;
    bigint_err_ty berr = bigint_bin_init(&t);
    if (berr != BIGINT_OK_E) {
        return berr;
    }

    const size_t n = bigint_bin_bit_len(x);
    const size_t k = (n + 1U) / 2U;  // 结果位数 = ceil(n / 2)
    for (size_t i = k; i > 0U; i--) {
        // rem = 4·rem + x 的位 (2i−1, 2i−2)（0-based 位索引）
        berr = bigint_bin_shl(rem, rem, 2U);
        if (berr != BIGINT_OK_E) {
            break;
        }
        const size_t hi = 2U * i - 1U;
        if ((hi < n) && bigint_bin_bit_test(x, hi)) {
            berr = bigint_bin_bit_set(rem, 1U, true);
            if (berr != BIGINT_OK_E) {
                break;
            }
        }
        if (bigint_bin_bit_test(x, hi - 1U)) {
            berr = bigint_bin_bit_set(rem, 0U, true);
            if (berr != BIGINT_OK_E) {
                break;
            }
        }
        berr = bigint_bin_shl(q, q, 1U);
        if (berr != BIGINT_OK_E) {
            break;
        }
        // t = 2·q + 1（q 为左移后值；注意是 (q<<1)|1，不能只置位 0——
        // 那得到的是 q|1，缺 ×2，历史缺陷）
        berr = bigint_bin_shl(&t, q, 1U);
        if (berr != BIGINT_OK_E) {
            break;
        }
        berr = bigint_bin_bit_set(&t, 0U, true);
        if (berr != BIGINT_OK_E) {
            break;
        }
        if (bigint_bin_cmp(&t, rem) <= 0) {
            berr = bigint_bin_sub(rem, rem, &t);
            if (berr != BIGINT_OK_E) {
                break;
            }
            berr = bigint_bin_bit_set(q, 0U, true);
            if (berr != BIGINT_OK_E) {
                break;
            }
        }
    }
    bigint_bin_free(&t);
    return berr;
}

/*
 * brief: 常规加法/减法核心（两操作数均为正常值，非零非无穷非 NaN）
 * param: dst      目标对象（允许与 lhs / rhs 别名）
 * param: lhs      被加数 / 被减数
 * param: rhs      加数 / 减数
 * param: subtract 为 true 时计算 lhs + (−rhs)
 * param: ctx      精度上下文
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 *         （dst 不变）
 * note: 精确对齐到 min(e1, e2)，整数加减后一次舍入（见文件头注释）
 */
static bigfloat_err_ty add_normal(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, bool subtract, const bigfloat_ctx_ty *ctx)
{
    const int64_t e1 = lhs->exp;
    const int64_t e2 = rhs->exp;
    const bool neg1 = (lhs->flag == BIGFLOAT_NEG_E);
    const bool neg2 = ((rhs->flag == BIGFLOAT_NEG_E) != subtract);

    const int64_t e_align = (e1 < e2) ? e1 : e2;

    bigint_bin_ty m1;
    bigint_bin_ty m2;
    bigint_bin_ty sum;
    bigint_err_ty berr;
    bigfloat_err_ty ferr = BIGFLOAT_OK_E;
    bigint_err_ty btmp = bigint_bin_init(&m1);
    if (btmp != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&m2);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&m1);
        return BIGFLOAT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&sum);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&m1);
        bigint_bin_free(&m2);
        return BIGFLOAT_ERR_OOM_E;
    }

    if (e1 > e_align) {
        berr = bigint_bin_shl(&m1, &lhs->mant, (size_t)(e1 - e_align));
    } else {
        berr = bigint_bin_copy(&m1, &lhs->mant);
    }
    if (berr != BIGINT_OK_E) {
        ferr = map_bigint_err(berr);
        goto cleanup;
    }
    if (e2 > e_align) {
        berr = bigint_bin_shl(&m2, &rhs->mant, (size_t)(e2 - e_align));
    } else {
        berr = bigint_bin_copy(&m2, &rhs->mant);
    }
    if (berr != BIGINT_OK_E) {
        ferr = map_bigint_err(berr);
        goto cleanup;
    }

    int sign = 1;
    if (neg1 == neg2) {
        // 同号：幅值相加
        berr = bigint_bin_add(&sum, &m1, &m2);
        if (berr != BIGINT_OK_E) {
            ferr = map_bigint_err(berr);
            goto cleanup;
        }
        sign = neg1 ? -1 : 1;
    } else {
        // 异号：幅值大减小
        const int mag_cmp = bigint_bin_cmp(&m1, &m2);
        if (mag_cmp > 0) {
            berr = bigint_bin_sub(&sum, &m1, &m2);
            if (berr != BIGINT_OK_E) {
                ferr = map_bigint_err(berr);
                goto cleanup;
            }
            sign = neg1 ? -1 : 1;
        } else if (mag_cmp < 0) {
            berr = bigint_bin_sub(&sum, &m2, &m1);
            if (berr != BIGINT_OK_E) {
                ferr = map_bigint_err(berr);
                goto cleanup;
            }
            sign = neg2 ? -1 : 1;
        } else {
            // 精确零：x + (−x)（含 x − x）→ 最近舍入 +0；向 −∞ 舍入 −0（§11）
            bigint_bin_free(&m1);
            bigint_bin_free(&m2);
            bigint_bin_free(&sum);
            set_special(dst, (ctx->round == BIGFLOAT_ROUND_TOWARD_NEG_E)
                    ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
            return BIGFLOAT_OK_E;
        }
    }

    ferr = nex_bf_round_pack(dst, &sum, e_align, false, sign, ctx);
    if (ferr != BIGFLOAT_OK_E) {
        goto cleanup;
    }

cleanup:
    bigint_bin_free(&m1);
    bigint_bin_free(&m2);
    bigint_bin_free(&sum);
    return ferr;
}

/* ------------------------------------------------------------------ */
/* 生命周期与上下文（§7.4）                                            */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化为 +0
 */
bigfloat_err_ty bigfloat_init(bigfloat_ty *val)
{
    if (val == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    val->flag = BIGFLOAT_POS_ZERO_E;
    val->exp = 0;
    const bigint_err_ty berr = bigint_bin_init(&val->mant);
    if (berr != BIGINT_OK_E) {
        return map_bigint_err(berr);
    }
    return BIGFLOAT_OK_E;
}

/*
 * brief: 释放浮点数占用的内存
 */
void bigfloat_free(bigfloat_ty *val)
{
    if (val == NULL) {
        return;
    }
    bigint_bin_free(&val->mant);
    val->exp = 0;
    val->flag = BIGFLOAT_POS_ZERO_E;
}

/*
 * brief: 深拷贝浮点数
 */
bigfloat_err_ty bigfloat_copy(bigfloat_ty *dst, const bigfloat_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    return copy_value(dst, src);
}

/*
 * brief: 构造精度上下文
 */
bigfloat_err_ty bigfloat_ctx_make(bigfloat_ctx_ty *dst, size_t mant_bits,
        size_t exp_bits, bigfloat_round_ty round)
{
    if (dst == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if ((mant_bits < 2U) || (exp_bits < 1U)
            || (exp_bits > BIGFLOAT_MAX_EXP_BITS)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if ((round < BIGFLOAT_ROUND_NEAREST_EVEN_E)
            || (round > BIGFLOAT_ROUND_AWAY_ZERO_E)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    dst->mant_bits = mant_bits;
    dst->exp_bits = exp_bits;
    dst->round = round;
    return BIGFLOAT_OK_E;
}

/*
 * brief: IEEE 754 单精度（binary32）预设上下文
 * note: p = 24 与 IEEE 一致；指数范围按 §7.3 公式由 8 位指数字段推导
 */
bigfloat_ctx_ty bigfloat_ctx_binary32(void)
{
    bigfloat_ctx_ty ctx;
    ctx.mant_bits = 24U;
    ctx.exp_bits = 8U;
    ctx.round = BIGFLOAT_ROUND_NEAREST_EVEN_E;
    return ctx;
}

/*
 * brief: IEEE 754 双精度（binary64）预设上下文
 */
bigfloat_ctx_ty bigfloat_ctx_binary64(void)
{
    bigfloat_ctx_ty ctx;
    ctx.mant_bits = 53U;
    ctx.exp_bits = 11U;
    ctx.round = BIGFLOAT_ROUND_NEAREST_EVEN_E;
    return ctx;
}

/*
 * brief: IEEE 754 四倍精度（binary128）预设上下文
 */
bigfloat_ctx_ty bigfloat_ctx_binary128(void)
{
    bigfloat_ctx_ty ctx;
    ctx.mant_bits = 113U;
    ctx.exp_bits = 15U;
    ctx.round = BIGFLOAT_ROUND_NEAREST_EVEN_E;
    return ctx;
}

/* ------------------------------------------------------------------ */
/* 分类断言（§7.4）                                                    */
/* ------------------------------------------------------------------ */

bool bigfloat_is_zero(const bigfloat_ty *val)
{
    if (val == NULL) {
        return false;
    }
    return (val->flag == BIGFLOAT_POS_ZERO_E)
            || (val->flag == BIGFLOAT_NEG_ZERO_E);
}

bool bigfloat_is_inf(const bigfloat_ty *val)
{
    if (val == NULL) {
        return false;
    }
    return (val->flag == BIGFLOAT_POS_INF_E)
            || (val->flag == BIGFLOAT_NEG_INF_E);
}

bool bigfloat_is_nan(const bigfloat_ty *val)
{
    return (val != NULL) && (val->flag == BIGFLOAT_NAN_E);
}

bool bigfloat_is_normal(const bigfloat_ty *val)
{
    if (val == NULL) {
        return false;
    }
    return (val->flag == BIGFLOAT_POS_E) || (val->flag == BIGFLOAT_NEG_E);
}

/* ------------------------------------------------------------------ */
/* 舍入打包（内部共享，nex_bigfloat_str.c 亦使用）                     */
/* ------------------------------------------------------------------ */

/*
 * brief: 按 ctx 舍入与规范化 value = raw_mant × 2^raw_exp + sticky_extra
 * note: 舍入决策（raw_mant 位长 bl > mant_bits 时右移 r = bl − mant_bits）：
 *       - 最近偶：round_bit（第 r−1 位）为 1，且低位非零 / 粘位 / 截断
 *         尾数最低位为 1 三者任一成立时进位（ties-to-even）；
 *       - 向 ±∞ / 远离零：被移出位非零或粘位时进位；
 *       - 向零：不进位。
 *       bl ≤ mant_bits 时左移补足，sticky_extra 忽略——调用方契约：
 *       sticky_extra 仅在 bl > mant_bits 时有效（add / sub 精确对齐、
 *       mul 乘积位长 ≥ 2p、div / sqrt / from_decimal 商位长 = p + 2
 *       均满足该契约）。上溢按舍入方向产生 ±∞ 或最大有限值；下溢
 *       flush-to-zero 产生 ±0，零符号 = sign。
 */
bigfloat_err_ty nex_bf_round_pack(bigfloat_ty *dst, const bigint_bin_ty *raw_mant,
        int64_t raw_exp, bool sticky_extra, int sign, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (raw_mant == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    const size_t mant_bits = ctx->mant_bits;

    bigint_bin_ty work;
    bigint_err_ty berr = bigint_bin_init(&work);
    if (berr != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_copy(&work, raw_mant);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&work);
        return map_bigint_err(berr);
    }

    const size_t bl = bigint_bin_bit_len(&work);
    int64_t exp = raw_exp;

    if (bl == 0U) {
        // 精确零（sticky 不应出现；防御性忽略）
        bigint_bin_free(&work);
        set_special(dst, (sign < 0) ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        return BIGFLOAT_OK_E;
    }

    if (bl > mant_bits) {
        // 需要右移舍入（情形 A，sticky_extra 有效）
        const size_t r = bl - mant_bits;
        const bool removed_nonzero = low_bits_nonzero(&work, r);
        const bool round_bit = bigint_bin_bit_test(&work, r - 1U);
        const bool tie_low_zero = !low_bits_nonzero(&work, r - 1U);
        berr = bigint_bin_shr(&work, &work, r);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&work);
            return map_bigint_err(berr);
        }
        const bool truncated_odd = bigint_bin_bit_test(&work, 0U);
        bool round_up = false;
        switch (ctx->round) {
            case BIGFLOAT_ROUND_NEAREST_EVEN_E:
                round_up = round_bit
                        && (!tie_low_zero || sticky_extra || truncated_odd);
                break;
            case BIGFLOAT_ROUND_TOWARD_ZERO_E:
                round_up = false;
                break;
            case BIGFLOAT_ROUND_TOWARD_POS_E:
                round_up = (sign > 0)
                        && (removed_nonzero || sticky_extra);
                break;
            case BIGFLOAT_ROUND_TOWARD_NEG_E:
                round_up = (sign < 0)
                        && (removed_nonzero || sticky_extra);
                break;
            case BIGFLOAT_ROUND_AWAY_ZERO_E:
                round_up = removed_nonzero || sticky_extra;
                break;
            default:
                bigint_bin_free(&work);
                return BIGFLOAT_ERR_INVALID_E;  // valid_ctx 已保证不可达
        }
        exp = raw_exp + (int64_t)r;
        if (round_up) {
            bigint_bin_ty one;
            berr = bigint_bin_init(&one);
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&work);
                return BIGFLOAT_ERR_OOM_E;
            }
            berr = bigint_bin_from_u64(&one, UINT64_C(1));
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&one);
                bigint_bin_free(&work);
                return map_bigint_err(berr);
            }
            berr = bigint_bin_add(&work, &work, &one);
            bigint_bin_free(&one);
            if (berr != BIGINT_OK_E) {
                bigint_bin_free(&work);
                return map_bigint_err(berr);
            }
            if (bigint_bin_bit_len(&work) > mant_bits) {
                // 进位溢出尾数：右移 1 位（精确），指数 +1
                berr = bigint_bin_shr(&work, &work, 1U);
                if (berr != BIGINT_OK_E) {
                    bigint_bin_free(&work);
                    return map_bigint_err(berr);
                }
                exp += 1;
            }
        }
    } else if (bl < mant_bits) {
        // 左移到恰好 mant_bits 位（精确；sticky_extra 忽略，见契约）
        const size_t lshift = mant_bits - bl;
        berr = bigint_bin_shl(&work, &work, lshift);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&work);
            return map_bigint_err(berr);
        }
        exp = raw_exp - (int64_t)lshift;
    }
    // bl == mant_bits：尾数无需调整

    // 指数范围检查（§7.3）
    int64_t emin;
    int64_t emax;
    ctx_exp_range(ctx, &emin, &emax);
    if (exp > emax) {
        // 上溢：按舍入方向产生 ±∞ 或最大有限值
        bool to_inf = false;
        switch (ctx->round) {
            case BIGFLOAT_ROUND_NEAREST_EVEN_E:
            case BIGFLOAT_ROUND_AWAY_ZERO_E:
                to_inf = true;
                break;
            case BIGFLOAT_ROUND_TOWARD_POS_E:
                to_inf = (sign > 0);
                break;
            case BIGFLOAT_ROUND_TOWARD_NEG_E:
                to_inf = (sign < 0);
                break;
            default:
                to_inf = false;  // TOWARD_ZERO
                break;
        }
        if (to_inf) {
            bigint_bin_free(&work);
            set_special(dst, (sign < 0) ? BIGFLOAT_NEG_INF_E
                    : BIGFLOAT_POS_INF_E);
            return BIGFLOAT_OK_E;
        }
        berr = max_finite_mant(&work, mant_bits);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&work);
            return map_bigint_err(berr);
        }
        exp = emax;
    } else if (exp < emin) {
        // 下溢：flush-to-zero。零符号（IEEE 754-2008 §7.5，设计 §7.3）：
        // 向 +∞ 舍入恒为 +0；其余模式取精确结果符号（向 −∞ 的负下溢为 −0）
        const bool to_neg_zero = (sign < 0)
                && (ctx->round != BIGFLOAT_ROUND_TOWARD_POS_E);
        bigint_bin_free(&work);
        set_special(dst, to_neg_zero ? BIGFLOAT_NEG_ZERO_E
                : BIGFLOAT_POS_ZERO_E);
        return BIGFLOAT_OK_E;
    }

    // 输出（接管 work，dst 与源别名安全：源已在 work 中独立）
    bigfloat_free(dst);
    bigint_bin_move(&dst->mant, &work);
    dst->exp = exp;
    dst->flag = (sign < 0) ? BIGFLOAT_NEG_E : BIGFLOAT_POS_E;
    return BIGFLOAT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 构造与转换（§7.4）                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: 从大整数构造浮点数，按 ctx 舍入
 */
bigfloat_err_ty bigfloat_from_bigint(bigfloat_ty *dst, const bigint_bin_ty *src,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    const int sign = (bigint_bin_sign(src) == BIGINT_SIGN_NEG_E) ? -1 : 1;
    return nex_bf_round_pack(dst, src, 0, false, sign, ctx);
}

/*
 * brief: 从 double 构造浮点数，总是精确（IEEE 754 位布局解析）
 */
bigfloat_err_ty bigfloat_from_f64(bigfloat_ty *dst, double value)
{
    if (dst == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));  // 位模式（与字节序无关）
    const bool negative = (bits >> 63U) != 0U;
    const uint64_t exp_field = (bits >> 52U) & 0x7FFU;
    const uint64_t frac = bits & UINT64_C(0xFFFFFFFFFFFFF);

    bigfloat_ty temp;
    bigfloat_init(&temp);
    bigint_err_ty berr;

    if (exp_field == 0x7FFU) {
        temp.flag = (frac == 0U)
                ? (negative ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E)
                : BIGFLOAT_NAN_E;
        temp.exp = 0;
    } else if ((exp_field == 0U) && (frac == 0U)) {
        temp.flag = negative ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E;
        temp.exp = 0;
    } else if (exp_field == 0U) {
        // 次正规：value = frac × 2^−1074，左移规范化到 53 位
        temp.flag = negative ? BIGFLOAT_NEG_E : BIGFLOAT_POS_E;
        berr = bigint_bin_from_u64(&temp.mant, frac);
        if (berr != BIGINT_OK_E) {
            bigfloat_free(&temp);
            return map_bigint_err(berr);
        }
        const size_t bl = bigint_bin_bit_len(&temp.mant);
        const size_t lshift = 53U - bl;
        berr = bigint_bin_shl(&temp.mant, &temp.mant, lshift);
        if (berr != BIGINT_OK_E) {
            bigfloat_free(&temp);
            return map_bigint_err(berr);
        }
        temp.exp = -1074 - (int64_t)lshift;
    } else {
        // 正常：value = (2^52 + frac) × 2^(exp_field − 1023 − 52)
        temp.flag = negative ? BIGFLOAT_NEG_E : BIGFLOAT_POS_E;
        berr = bigint_bin_from_u64(&temp.mant, (UINT64_C(1) << 52U) | frac);
        if (berr != BIGINT_OK_E) {
            bigfloat_free(&temp);
            return map_bigint_err(berr);
        }
        temp.exp = (int64_t)exp_field - 1075;
    }

    bigfloat_free(dst);
    *dst = temp;
    return BIGFLOAT_OK_E;
}

/*
 * brief: 转为 double（最近偶舍入，与 ctx 无关）
 */
bigfloat_err_ty bigfloat_to_f64(const bigfloat_ty *src, double *out)
{
    if ((src == NULL) || (out == NULL)) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    uint64_t bits = 0U;
    if (bigfloat_is_nan(src)) {
        bits = UINT64_C(0x7FF8000000000000);  // 默认 quiet NaN
    } else if (bigfloat_is_inf(src)) {
        bits = (src->flag == BIGFLOAT_NEG_INF_E)
                ? UINT64_C(0xFFF0000000000000) : UINT64_C(0x7FF0000000000000);
    } else if (bigfloat_is_zero(src)) {
        bits = (src->flag == BIGFLOAT_NEG_ZERO_E)
                ? UINT64_C(0x8000000000000000) : 0U;
    } else {
        // 正常值：nearest-even 舍入到 double（IEEE 754 默认舍入）
        const bigint_bin_ty *m = &src->mant;
        const int64_t e = src->exp;
        const size_t bl = bigint_bin_bit_len(m);
        const uint64_t sign_bit = (src->flag == BIGFLOAT_NEG_E)
                ? UINT64_C(0x8000000000000000) : 0U;
        uint64_t fraction = 0U;
        uint64_t exponent_field = 0U;

        // 防御：指数远超 double 范围时避免 top_e 溢出 int64
        if (e > 1024) {
            bits = sign_bit | UINT64_C(0x7FF0000000000000);
            memcpy(out, &bits, sizeof(*out));
            *out = (src->flag == BIGFLOAT_NEG_E) ? -HUGE_VAL : HUGE_VAL;
            return BIGFLOAT_ERR_OVERFLOW_E;
        }
        if (e < -1074 - (int64_t)bl - 1) {
            bits = sign_bit;
            memcpy(out, &bits, sizeof(*out));
            return BIGFLOAT_OK_E;  // 下溢到 ±0
        }

        const int64_t top_e = e + (int64_t)bl - 1;
        if (top_e >= -1022) {
            // normal：尾数 53 位（含隐含位）
            if (bl > 53U) {
                const size_t r = bl - 53U;
                const bigint_err_ty berr = shift_round_nearest(&fraction, m, r);
                if (berr != BIGINT_OK_E) {
                    return map_bigint_err(berr);
                }
                if (fraction >= (UINT64_C(1) << 53U)) {
                    // 进位到 54 位：右移 1 位，指数 +1
                    fraction >>= 1U;
                    if (top_e + 1 > 1023) {
                        bits = sign_bit | UINT64_C(0x7FF0000000000000);
                        memcpy(out, &bits, sizeof(*out));
                        *out = (src->flag == BIGFLOAT_NEG_E)
                                ? -HUGE_VAL : HUGE_VAL;
                        return BIGFLOAT_ERR_OVERFLOW_E;
                    }
                    exponent_field = (uint64_t)(top_e + 1 + 1023);
                } else {
                    exponent_field = (uint64_t)(top_e + 1023);
                }
                fraction &= (UINT64_C(1) << 52U) - 1U;
            } else {
                // 精确（位长 ≤ 53，无舍入）
                uint64_t m64 = 0U;
                const bigint_err_ty berr = bigint_bin_to_u64(m, &m64);
                if (berr != BIGINT_OK_E) {
                    return map_bigint_err(berr);
                }
                // frac53 = m64 << lshift（53 位，最高位为隐含 1），
                // fraction = frac53 的低 52 位（掩码去掉隐含位；
                // 注意不能用右移 1：bl == 53 时会把隐含位带进 fraction）
                const size_t lshift = 53U - bl;
                const uint64_t frac53 = m64 << lshift;
                fraction = frac53 & ((UINT64_C(1) << 52U) - 1U);
                exponent_field = (uint64_t)(top_e + 1023);
            }
        } else {
            // subnormal：value = fraction × 2^−1074
            const int64_t f_shift = e + 1074;
            if (f_shift >= 0) {
                // 精确整数（值 < 2^−1022 保证 fraction < 2^52）
                uint64_t m64 = 0U;
                const bigint_err_ty berr = bigint_bin_to_u64(m, &m64);
                if (berr != BIGINT_OK_E) {
                    return map_bigint_err(berr);
                }
                fraction = m64 << (unsigned)f_shift;
                exponent_field = 0U;
            } else {
                const size_t r = (size_t)(-f_shift);
                const bigint_err_ty berr = shift_round_nearest(&fraction, m, r);
                if (berr != BIGINT_OK_E) {
                    return map_bigint_err(berr);
                }
                if (fraction >= (UINT64_C(1) << 52U)) {
                    // 舍入进位到最小 normal：exponent = 1，fraction = 0
                    exponent_field = 1U;
                    fraction = 0U;
                } else {
                    exponent_field = 0U;
                }
            }
        }
        bits = sign_bit | (exponent_field << 52U)
                | (fraction & ((UINT64_C(1) << 52U) - 1U));
    }

    memcpy(out, &bits, sizeof(*out));
    return BIGFLOAT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 算术（§7.4；特殊值传播见 §11）                                      */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs
 */
bigfloat_err_ty bigfloat_add(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(lhs) || bigfloat_is_nan(rhs)) {
        set_special(dst, BIGFLOAT_NAN_E);
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_inf(lhs) || bigfloat_is_inf(rhs)) {
        if (bigfloat_is_inf(lhs) && bigfloat_is_inf(rhs)
                && (lhs->flag != rhs->flag)) {
            set_special(dst, BIGFLOAT_NAN_E);  // +∞ + −∞
        } else {
            set_special(dst, bigfloat_is_inf(lhs) ? lhs->flag : rhs->flag);
        }
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_zero(lhs) && bigfloat_is_zero(rhs)) {
        // 零符号规则（§11）：同号取该符号；异号最近舍入 +0，向 −∞ 舍入 −0
        if (lhs->flag == rhs->flag) {
            set_special(dst, lhs->flag);
        } else {
            set_special(dst, (ctx->round == BIGFLOAT_ROUND_TOWARD_NEG_E)
                    ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        }
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_zero(lhs)) {
        return copy_value(dst, rhs);  // 0 + x = x
    }
    if (bigfloat_is_zero(rhs)) {
        return copy_value(dst, lhs);  // x + 0 = x
    }
    return add_normal(dst, lhs, rhs, false, ctx);
}

/*
 * brief: 减法 dst = lhs − rhs
 */
bigfloat_err_ty bigfloat_sub(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(lhs) || bigfloat_is_nan(rhs)) {
        set_special(dst, BIGFLOAT_NAN_E);
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_inf(lhs) || bigfloat_is_inf(rhs)) {
        if (bigfloat_is_inf(lhs) && bigfloat_is_inf(rhs)
                && (lhs->flag == rhs->flag)) {
            set_special(dst, BIGFLOAT_NAN_E);  // +∞ − +∞
        } else if (bigfloat_is_inf(lhs)) {
            set_special(dst, lhs->flag);  // ∞ − 有限 = ∞
        } else {
            set_special(dst, (rhs->flag == BIGFLOAT_POS_INF_E)
                    ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E);  // 有限 − ∞ = −∞
        }
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_zero(lhs) && bigfloat_is_zero(rhs)) {
        // 0 − 0：同号取被减数符号；异号最近舍入 +0，向 −∞ 舍入 −0（§11）
        if (lhs->flag == rhs->flag) {
            set_special(dst, lhs->flag);
        } else {
            set_special(dst, (ctx->round == BIGFLOAT_ROUND_TOWARD_NEG_E)
                    ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        }
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_zero(lhs)) {
        bigfloat_err_ty err = copy_value(dst, rhs);  // 0 − x = −x
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_neg(dst);
        }
        return err;
    }
    if (bigfloat_is_zero(rhs)) {
        return copy_value(dst, lhs);  // x − 0 = x
    }
    return add_normal(dst, lhs, rhs, true, ctx);
}

/*
 * brief: 乘法 dst = lhs × rhs
 */
bigfloat_err_ty bigfloat_mul(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(lhs) || bigfloat_is_nan(rhs)) {
        set_special(dst, BIGFLOAT_NAN_E);
        return BIGFLOAT_OK_E;
    }
    const bool neg = flag_is_negative(lhs->flag) != flag_is_negative(rhs->flag);
    if (bigfloat_is_inf(lhs) || bigfloat_is_inf(rhs)) {
        if (bigfloat_is_zero(lhs) || bigfloat_is_zero(rhs)) {
            set_special(dst, BIGFLOAT_NAN_E);  // 0 × ∞
        } else {
            set_special(dst, neg ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E);
        }
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_zero(lhs) || bigfloat_is_zero(rhs)) {
        set_special(dst, neg ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        return BIGFLOAT_OK_E;
    }

    // 常规：尾数乘、指数加（指数和溢出防御）
    const int64_t e1 = lhs->exp;
    const int64_t e2 = rhs->exp;
    int64_t raw_exp;
    bool exp_overflow = false;
    bool exp_underflow = false;
    if ((e1 > 0) && (e2 > 0) && (e1 > INT64_MAX - e2)) {
        exp_overflow = true;
    } else if ((e1 < 0) && (e2 < 0) && (e1 < INT64_MIN - e2)) {
        exp_underflow = true;
    } else {
        raw_exp = e1 + e2;
    }

    bigint_bin_ty prod;
    bigint_err_ty berr = bigint_bin_init(&prod);
    if (berr != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_mul(&prod, &lhs->mant, &rhs->mant);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&prod);
        return map_bigint_err(berr);
    }

    bigfloat_err_ty ferr;
    if (exp_overflow) {
        // 指数和溢出：借用 round_pack 的上溢路径（emax + 1 触发）
        int64_t emax;
        int64_t emin;
        ctx_exp_range(ctx, &emin, &emax);
        ferr = nex_bf_round_pack(dst, &prod, emax + 1, false,
                neg ? -1 : 1, ctx);
    } else if (exp_underflow) {
        int64_t emax;
        int64_t emin;
        ctx_exp_range(ctx, &emin, &emax);
        ferr = nex_bf_round_pack(dst, &prod, emin - 1, false,
                neg ? -1 : 1, ctx);
    } else {
        ferr = nex_bf_round_pack(dst, &prod, raw_exp, false,
                neg ? -1 : 1, ctx);
    }
    bigint_bin_free(&prod);
    return ferr;
}

/*
 * brief: 除法 dst = lhs / rhs
 */
bigfloat_err_ty bigfloat_div(bigfloat_ty *dst, const bigfloat_ty *lhs,
        const bigfloat_ty *rhs, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(lhs) || bigfloat_is_nan(rhs)) {
        set_special(dst, BIGFLOAT_NAN_E);
        return BIGFLOAT_OK_E;
    }
    const bool neg = flag_is_negative(lhs->flag) != flag_is_negative(rhs->flag);
    const bool lhs_zero = bigfloat_is_zero(lhs);
    const bool rhs_zero = bigfloat_is_zero(rhs);
    if (rhs_zero) {
        // 有限 / ±0 → ±∞；0 / 0 → NaN（§11）
        set_special(dst, lhs_zero ? BIGFLOAT_NAN_E
                : (neg ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E));
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_inf(lhs) || bigfloat_is_inf(rhs)) {
        if (bigfloat_is_inf(lhs) && bigfloat_is_inf(rhs)) {
            set_special(dst, BIGFLOAT_NAN_E);  // ∞ / ∞
        } else if (bigfloat_is_inf(lhs)) {
            set_special(dst, neg ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E);
        } else {
            set_special(dst, neg ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        }
        return BIGFLOAT_OK_E;
    }
    if (lhs_zero) {
        set_special(dst, neg ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E);
        return BIGFLOAT_OK_E;
    }

    // 常规：被除数/除数缩放使商位长 = mant_bits + 2（含保护位，正确舍入）
    const int64_t e1 = lhs->exp;
    const int64_t e2 = rhs->exp;
    const size_t bl1 = bigint_bin_bit_len(&lhs->mant);
    const size_t bl2 = bigint_bin_bit_len(&rhs->mant);
    // shift = 目标商位长 − 自然商位长 = (mant_bits + 2) − (bl1 − bl2 + 1)
    const int64_t shift = (int64_t)ctx->mant_bits + 1 - (int64_t)bl1
            + (int64_t)bl2;

    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_ty quot;
    bigint_bin_ty rem;
    bigint_err_ty berr;
    bigint_err_ty btmp = bigint_bin_init(&num);
    if (btmp != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&den);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&num);
        return BIGFLOAT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&quot);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        return BIGFLOAT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&rem);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        bigint_bin_free(&quot);
        return BIGFLOAT_ERR_OOM_E;
    }

    if (shift >= 0) {
        berr = bigint_bin_shl(&num, &lhs->mant, (size_t)shift);
        if (berr != BIGINT_OK_E) {
            goto div_cleanup;
        }
        berr = bigint_bin_copy(&den, &rhs->mant);
        if (berr != BIGINT_OK_E) {
            goto div_cleanup;
        }
    } else {
        berr = bigint_bin_copy(&num, &lhs->mant);
        if (berr != BIGINT_OK_E) {
            goto div_cleanup;
        }
        berr = bigint_bin_shl(&den, &rhs->mant, (size_t)(-shift));
        if (berr != BIGINT_OK_E) {
            goto div_cleanup;
        }
    }
    berr = bigint_bin_div_rem(&quot, &rem, &num, &den);
    if (berr != BIGINT_OK_E) {
        goto div_cleanup;
    }
    {
        // 余数非零 → 商严格大于整数部分 → 粘位（商位长 > mant_bits，
        // 满足 round_pack 的 sticky 契约）
        const bool sticky = !bigint_bin_is_zero(&rem);
        // raw_exp = e1 − e2 − shift（shift 已并入 num/den 缩放）；
        // 分步防溢出：|shift| ≤ mant_bits + bl2 + 1 << 内存限制，
        // 无法抵消 e1 − e2 的 int64 级溢出 → 溢出即上溢/下溢
        int64_t raw_exp = 0;
        bool exp_overflow = false;
        bool exp_underflow = false;
        if ((e1 > 0) && (e2 < 0) && (e1 > INT64_MAX + e2)) {
            exp_overflow = true;  // e1 − e2 上溢，shift 无法抵消
        } else if ((e1 < 0) && (e2 > 0) && (e1 < INT64_MIN + e2)) {
            exp_underflow = true;  // e1 − e2 下溢
        } else {
            const int64_t dexp = e1 - e2;
            if ((dexp > 0) && (shift < 0) && (dexp > INT64_MAX + shift)) {
                exp_overflow = true;
            } else if ((dexp < 0) && (shift > 0)
                    && (dexp < INT64_MIN + shift)) {
                exp_underflow = true;
            } else {
                raw_exp = dexp - shift;
            }
        }

        bigfloat_err_ty ferr;
        if (exp_overflow) {
            int64_t emax;
            int64_t emin;
            ctx_exp_range(ctx, &emin, &emax);
            ferr = nex_bf_round_pack(dst, &quot, emax + 1, false,
                    neg ? -1 : 1, ctx);
        } else if (exp_underflow) {
            int64_t emax;
            int64_t emin;
            ctx_exp_range(ctx, &emin, &emax);
            ferr = nex_bf_round_pack(dst, &quot, emin - 1, sticky,
                    neg ? -1 : 1, ctx);
        } else {
            ferr = nex_bf_round_pack(dst, &quot, raw_exp, sticky,
                    neg ? -1 : 1, ctx);
        }
        berr = (ferr == BIGFLOAT_OK_E) ? BIGINT_OK_E
                : (ferr == BIGFLOAT_ERR_OOM_E) ? BIGINT_ERR_OOM_E
                : BIGINT_ERR_INVALID_E;
    }

div_cleanup:
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    bigint_bin_free(&quot);
    bigint_bin_free(&rem);
    return map_bigint_err(berr);
}

/*
 * brief: 平方根 dst = sqrt(src)
 */
bigfloat_err_ty bigfloat_sqrt(bigfloat_ty *dst, const bigfloat_ty *src,
        const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(src)) {
        set_special(dst, BIGFLOAT_NAN_E);
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_inf(src)) {
        set_special(dst, (src->flag == BIGFLOAT_NEG_INF_E)
                ? BIGFLOAT_NAN_E : BIGFLOAT_POS_INF_E);  // √−∞ = NaN
        return BIGFLOAT_OK_E;
    }
    if (bigfloat_is_zero(src)) {
        set_special(dst, src->flag);  // √±0 = ±0
        return BIGFLOAT_OK_E;
    }
    if (src->flag == BIGFLOAT_NEG_E) {
        set_special(dst, BIGFLOAT_NAN_E);  // 负数 → NaN
        return BIGFLOAT_OK_E;
    }

    // 常规：值 = mant × 2^exp；先把指数调整为偶数（Y × 2^(2k) 形式）
    bigint_bin_ty y;
    bigint_err_ty berr = bigint_bin_init(&y);
    if (berr != BIGINT_OK_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    int64_t e_adj;
    if ((src->exp & 1) != 0) {
        // exp 为奇：Y = mant × 2，e_adj = (exp − 1) / 2
        berr = bigint_bin_shl(&y, &src->mant, 1U);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&y);
            return map_bigint_err(berr);
        }
        e_adj = (src->exp - 1) / 2;
    } else {
        berr = bigint_bin_copy(&y, &src->mant);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&y);
            return map_bigint_err(berr);
        }
        e_adj = src->exp / 2;
    }

    // 缩放 Y 使 sqrt 位长 = mant_bits + 2（含保护位）
    const size_t target = 2U * (ctx->mant_bits + 2U);
    const size_t bl = bigint_bin_bit_len(&y);
    int64_t scale_shift = 0;
    bool shifted_out = false;
    if (bl > target) {
        // 右移偶数位（保留 sqrt 的整数性），移出位 → 粘位
        size_t r;
        if (((bl - target) & 1U) == 0U) {
            r = bl - target;
        } else {
            r = bl - (target - 1U);
        }
        shifted_out = low_bits_nonzero(&y, r);
        berr = bigint_bin_shr(&y, &y, r);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&y);
            return map_bigint_err(berr);
        }
        // y' = y >> r，q = floor(sqrt(y')) ≈ sqrt(y)·2^(−r/2)；
        // sqrt(value) = sqrt(y)·2^e_adj = q·2^(e_adj + r/2) → scale_shift = +r/2
        scale_shift = (int64_t)(r / 2U);
    } else if (bl < target - 1U) {
        // 左移偶数位使位长 ∈ {target−1, target}
        size_t l;
        if (((target - bl) & 1U) == 0U) {
            l = target - bl;
        } else {
            l = target - 1U - bl;
        }
        berr = bigint_bin_shl(&y, &y, l);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&y);
            return map_bigint_err(berr);
        }
        // y' = y << l，q = floor(sqrt(y')) ≈ sqrt(y)·2^(l/2)；
        // sqrt(value) = sqrt(y)·2^e_adj = q·2^(e_adj − l/2) → scale_shift = −l/2
        scale_shift = -(int64_t)(l / 2U);
    }

    // 整数平方根：q = floor(sqrt(Y))，rem = Y − q²
    bigint_bin_ty q;
    bigint_bin_ty rem;
    berr = bigint_bin_init(&q);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&y);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&rem);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&y);
        bigint_bin_free(&q);
        return BIGFLOAT_ERR_OOM_E;
    }
    berr = isqrt(&q, &rem, &y);
    bigint_bin_free(&y);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&q);
        bigint_bin_free(&rem);
        return map_bigint_err(berr);
    }

    // 余数非零 → 平方根严格大于整数部分 → 粘位（q 位长 = p + 2）
    const bool sticky = !bigint_bin_is_zero(&rem) || shifted_out;
    const int64_t raw_exp = e_adj + scale_shift;
    const bigfloat_err_ty ferr = nex_bf_round_pack(dst, &q, raw_exp, sticky,
            1, ctx);
    bigint_bin_free(&q);
    bigint_bin_free(&rem);
    return ferr;
}

/*
 * brief: 就地取负（翻转符号标志；NaN 不变）
 */
bigfloat_err_ty bigfloat_neg(bigfloat_ty *val)
{
    if (val == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    switch (val->flag) {
        case BIGFLOAT_POS_ZERO_E:
            val->flag = BIGFLOAT_NEG_ZERO_E;
            break;
        case BIGFLOAT_NEG_ZERO_E:
            val->flag = BIGFLOAT_POS_ZERO_E;
            break;
        case BIGFLOAT_POS_E:
            val->flag = BIGFLOAT_NEG_E;
            break;
        case BIGFLOAT_NEG_E:
            val->flag = BIGFLOAT_POS_E;
            break;
        case BIGFLOAT_POS_INF_E:
            val->flag = BIGFLOAT_NEG_INF_E;
            break;
        case BIGFLOAT_NEG_INF_E:
            val->flag = BIGFLOAT_POS_INF_E;
            break;
        case BIGFLOAT_NAN_E:
            break;  // NaN 不变
        default:
            return BIGFLOAT_ERR_INVALID_E;  // 防御：非法标志
    }
    return BIGFLOAT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 比较（§7.4）                                                        */
/* ------------------------------------------------------------------ */

/*
 * brief: 三路比较
 * return: lhs < rhs 为负，lhs == rhs 为 0，lhs > rhs 为正；
 *         任一操作数为 NaN 返回 2
 */
int bigfloat_cmp(const bigfloat_ty *lhs, const bigfloat_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return 2;  // 保守：视为 NaN 语义
    }
    if (bigfloat_is_nan(lhs) || bigfloat_is_nan(rhs)) {
        return 2;
    }
    const int lr = flag_rank(lhs->flag);
    const int rr = flag_rank(rhs->flag);
    if (lr != rr) {
        return (lr < rr) ? -1 : 1;
    }
    if (lr != 1 && lr != 3) {
        return 0;  // 同为 ±∞ 或同为 ±0：相等
    }
    const int mag_cmp = cmp_mag_exp(&lhs->mant, lhs->exp, &rhs->mant, rhs->exp);
    if (lhs->flag == BIGFLOAT_NEG_E) {
        return -mag_cmp;  // 负数：幅值大者反而小
    }
    return mag_cmp;
}

/*
 * brief: 相等判断（+0 == −0 为真；NaN 与任何值均不相等）
 */
bool bigfloat_eq(const bigfloat_ty *lhs, const bigfloat_ty *rhs)
{
    return bigfloat_cmp(lhs, rhs) == 0;
}

/* ------------------------------------------------------------------ */
/* 分解与合成（§7.4）                                                  */
/* ------------------------------------------------------------------ */

/*
 * brief: 分解浮点数为尾数、指数与标志
 */
bigfloat_err_ty bigfloat_decompose(const bigfloat_ty *src, bigint_bin_ty *mant,
        int64_t *exp, bigfloat_flag_ty *flag)
{
    if ((src == NULL) || (mant == NULL)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    const bigint_err_ty berr = bigint_bin_copy(mant, &src->mant);
    if (berr != BIGINT_OK_E) {
        return map_bigint_err(berr);
    }
    if (exp != NULL) {
        *exp = src->exp;
    }
    if (flag != NULL) {
        *flag = src->flag;
    }
    return BIGFLOAT_OK_E;
}

/*
 * brief: 合成浮点数，按 ctx 对尾数舍入/规范化
 */
bigfloat_err_ty bigfloat_compose(bigfloat_ty *dst, const bigint_bin_ty *mant,
        int64_t exp, bigfloat_flag_ty flag, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (mant == NULL) || !valid_ctx(ctx)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    switch (flag) {
        case BIGFLOAT_POS_ZERO_E:
        case BIGFLOAT_NEG_ZERO_E:
        case BIGFLOAT_POS_INF_E:
        case BIGFLOAT_NEG_INF_E:
        case BIGFLOAT_NAN_E:
            set_special(dst, flag);  // 特殊值忽略 mant / exp
            return BIGFLOAT_OK_E;
        case BIGFLOAT_POS_E:
        case BIGFLOAT_NEG_E:
            return nex_bf_round_pack(dst, mant, exp, false,
                    (flag == BIGFLOAT_NEG_E) ? -1 : 1, ctx);
        default:
            return BIGFLOAT_ERR_INVALID_E;  // 非法标志
    }
}
