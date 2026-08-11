/*
 * nex_bigdecimal.c：bigdecimal_ty（可调精度十进制浮点数）核心实现。
 *
 * 职责（设计文档 §8）：生命周期与上下文、分类断言、舍入打包
 * （nex_dec_round_pack）、四则运算与 sqrt、比较、分解与合成、
 * 与大整数互转。十进制字符串 I/O 由 nex_bigdecimal_str.c 实现。
 *
 * 语义要点（设计文档 §8.1 / §7.3 / §11）：
 *   - 正常值 value = ±mant × 10^exp；nex_dec_round_pack 输出的正常值
 *     尾数个位非 0（无尾随零），表示唯一；
 *   - 舍入：运算先精确求值，再按 ctx->round 一次舍入（无双重舍入）。
 *     舍入位（第 mant_digits+1 位数字）与低位粘性与调用方粘位标记
 *     （sticky）共同参与最近偶（ties-to-even）判定；
 *   - 上溢按舍入方向产生 ±∞ 或最大有限值；下溢 flush-to-zero，
 *     零符号按 IEEE 754-2008 §7.5（向 +∞ 舍入恒 +0）；
 *   - 加法/减法按指数差用 bigint_dec_mul_pow10 精确对齐（十进制移位
 *     为 O(n) 内存搬移，§8.3），整数加减后一次舍入；
 *   - 除法/开方：带保护位（mant_digits + 2 位）的整数除法/整数平方根，
 *     余数非零置粘位，正确舍入；
 *   - 特殊值传播真值表（NaN / ±∞ / ±0）见各运算注释。
 *
 * 规范化不变式（§8.1）：正常值尾数个位非 0；±0 / ±∞ / NaN 时 mant 与
 * exp 无意义（置零）。所有公开 API 的输出均满足。
 */

#include "nex/bigdecimal/nex_bigdecimal_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

/* 10^k（k ≤ 18，uint64 可表示） */
static uint64_t pow10u(size_t k)
{
    uint64_t v = 1;
    while (k > 0U) {
        v *= 10U;
        k--;
    }
    return v;
}

/*
 * brief: 校验精度上下文
 * return: ctx 合法返回 true
 * note: mant_digits ≥ 1、1 ≤ exp_digits ≤ 18、round 为合法枚举值
 */
static bool valid_ctx(const bigdecimal_ctx_ty *ctx)
{
    if (ctx == NULL) {
        return false;
    }
    if ((ctx->mant_digits < 1U)
            || (ctx->exp_digits < 1U)
            || (ctx->exp_digits > BIGDECIMAL_MAX_EXP_DIGITS)) {
        return false;
    }
    if ((ctx->round < BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            || (ctx->round > BIGDECIMAL_ROUND_AWAY_ZERO_E)) {
        return false;
    }
    return true;
}

/*
 * brief: 计算上下文指数范围 [emin, emax]
 * note: 范围公式 [−(10^exp_digits − 1), 10^exp_digits − 1]（§8.1）；
 *       exp_digits ≤ 18 保证 10^exp_digits − 1 不溢出 int64
 */
static void ctx_exp_range(const bigdecimal_ctx_ty *ctx, int64_t *emin,
        int64_t *emax)
{
    const int64_t m = (int64_t)pow10u(ctx->exp_digits) - 1;
    *emin = -m;
    *emax = m;
}

/*
 * brief: bigint 错误码映射为 bigdecimal 错误码（设计文档 §3.1）
 */
static bigdecimal_err_ty map_bigint_err(bigint_err_ty err)
{
    if (err == BIGINT_OK_E) {
        return BIGDECIMAL_OK_E;
    }
    if (err == BIGINT_ERR_OOM_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    return BIGDECIMAL_ERR_INVALID_E;
}

/*
 * brief: 将 dst 置为指定特殊值（±0 / ±∞ / NaN），释放原有尾数资源
 */
static void set_special(bigdecimal_ty *dst, bigdecimal_flag_ty flag)
{
    bigdecimal_free(dst);
    dst->flag = flag;
    dst->exp = 0;
}

/*
 * brief: 深拷贝 src 的全部字段到 dst
 * return: 成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E
 *         （dst 不变）
 */
static bigdecimal_err_ty copy_value(bigdecimal_ty *dst,
        const bigdecimal_ty *src)
{
    const bigint_err_ty berr = bigint_dec_copy(&dst->mant, &src->mant);
    if (berr != BIGINT_OK_E) {
        return map_bigint_err(berr);
    }
    dst->exp = src->exp;
    dst->flag = src->flag;
    return BIGDECIMAL_OK_E;
}

/*
 * brief: 标志是否为"负"（正常负值 / −0 / −∞；NaN 视为非负）
 */
static bool flag_is_negative(bigdecimal_flag_ty flag)
{
    return (flag == BIGDECIMAL_NEG_E) || (flag == BIGDECIMAL_NEG_ZERO_E)
            || (flag == BIGDECIMAL_NEG_INF_E);
}

/*
 * brief: 标志的全序秩：−∞ < 负 < ±0 < 正 < +∞
 */
static int flag_rank(bigdecimal_flag_ty flag)
{
    switch (flag) {
        case BIGDECIMAL_NEG_INF_E:
            return 0;
        case BIGDECIMAL_NEG_E:
            return 1;
        case BIGDECIMAL_POS_ZERO_E:
        case BIGDECIMAL_NEG_ZERO_E:
            return 2;
        case BIGDECIMAL_POS_E:
            return 3;
        case BIGDECIMAL_POS_INF_E:
            return 4;
        default:
            return 5;  // NaN
    }
}

/*
 * brief: 取尾数最后一位十进制数字（v 非零时）
 */
static unsigned last_digit(const bigint_dec_ty *v)
{
    uint64_t low = 0;
    if (bigint_dec_to_u64(v, &low) == BIGINT_OK_E) {
        return (unsigned)(low % 10U);
    }
    /* 值超 uint64：仍可求个位——通过 div_rem 10 */
    bigint_dec_ty ten;
    bigint_dec_ty rem;
    bigint_dec_init(&ten);
    bigint_dec_init(&rem);
    bigint_dec_from_u64(&ten, 10U);
    unsigned d = 0;
    if (bigint_dec_div_rem(NULL, &rem, v, &ten) == BIGINT_OK_E) {
        uint64_t rv = 0;
        if (bigint_dec_to_u64(&rem, &rv) == BIGINT_OK_E) {
            d = (unsigned)(rv % 10U);
        }
    }
    bigint_dec_free(&ten);
    bigint_dec_free(&rem);
    return d;
}

/*
 * brief: 去除尾随十进制零（就地），返回移除的零个数
 * note: 二分最大 k：val % 10^k == 0（div_pow10 + mul_pow10 往返比较），
 *       O(log n) 次 O(n) 操作
 */
static size_t strip_trailing_zeros(bigint_dec_ty *val)
{
    const size_t dl = bigint_dec_digit_len(val);
    if (dl <= 1U) {
        return 0U;
    }
    size_t lo = 0U;
    size_t hi = dl - 1U;  // 尾随零数 ≤ dl − 1
    bigint_dec_ty tmp;
    if (bigint_dec_init(&tmp) != BIGINT_OK_E) {
        return 0U;  // OOM 防御：保持原值（规范化不变量破坏，但 OOM 下优先安全）
    }
    while (lo < hi) {
        const size_t mid = lo + (hi - lo + 1U) / 2U;
        bigint_dec_copy(&tmp, val);
        bigint_dec_div_pow10(&tmp, mid);
        bigint_dec_mul_pow10(&tmp, mid);
        if (bigint_dec_cmp(&tmp, val) == 0) {
            lo = mid;
        } else {
            hi = mid - 1U;
        }
    }
    bigint_dec_free(&tmp);
    if (lo > 0U) {
        bigint_dec_div_pow10(val, lo);
    }
    return lo;
}

/*
 * brief: 构造最大有限值尾数 10^mant_digits − 1
 */
static bigint_err_ty max_finite_mant(bigint_dec_ty *dst, size_t mant_digits)
{
    bigint_err_ty err = bigint_dec_from_u64(dst, 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_dec_mul_pow10(dst, mant_digits);
    if (err != BIGINT_OK_E) {
        return err;
    }
    bigint_dec_ty one;
    err = bigint_dec_init(&one);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_dec_from_u64(&one, 1U);
    if (err == BIGINT_OK_E) {
        err = bigint_dec_sub(dst, dst, &one);
    }
    bigint_dec_free(&one);
    return err;
}

/* ------------------------------------------------------------------ */
/* 舍入打包（内部共享，nex_bigdecimal_str.c 亦使用）                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 按 ctx 舍入与规范化 value = ±raw_mant × 10^raw_exp + 粘位
 * note: 舍入决策（raw_mant 位数 dl > mant_digits 时舍弃 drop = dl − mant_digits
 *       位）：舍入位 = 第 (mant_digits+1) 位数字；粘位 = 低位任意非零
 *       或调用方 sticky：
 *       - 最近偶：舍入位 > 5，或 = 5 且（低位非零 / 粘位 / 截断尾数末位
 *         为奇）三者任一成立时进位（ties-to-even）；
 *       - 向 ±∞ / 远离零：被移出位非零或粘位时进位；
 *       - 向零：不进位。
 *       dl ≤ mant_digits 时精确（sticky 应为 false；防御性处理见实现）。
 *       上溢按舍入方向产生 ±∞ 或最大有限值；下溢 flush-to-zero，
 *       零符号 = 精确结果符号（向 +∞ 舍入恒 +0，IEEE 754-2008 §7.5）。
 */
bigdecimal_err_ty nex_dec_round_pack(bigdecimal_ty *dst,
        const bigint_dec_ty *raw_mant, int64_t raw_exp, bool sticky, int sign,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (raw_mant == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    const size_t mant_digits = ctx->mant_digits;

    bigint_dec_ty work;
    bigint_err_ty berr = bigint_dec_init(&work);
    if (berr != BIGINT_OK_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    berr = bigint_dec_copy(&work, raw_mant);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&work);
        return map_bigint_err(berr);
    }
    /* 尾数为幅值：防御性取绝对值（from_bigint / compose 可能传入负值） */
    if (bigint_dec_sign(&work) == BIGINT_SIGN_NEG_E) {
        bigint_dec_abs(&work);
    }

    int64_t exp = raw_exp;

    if (bigint_dec_is_zero(&work)) {
        // 精确零（sticky 不应出现；防御性忽略）
        bigint_dec_free(&work);
        set_special(dst, (sign < 0) ? BIGDECIMAL_NEG_ZERO_E
                : BIGDECIMAL_POS_ZERO_E);
        return BIGDECIMAL_OK_E;
    }

    const size_t dl = bigint_dec_digit_len(&work);

    if (dl > mant_digits) {
        // 需要舍弃尾部数字（情形 A，sticky 有效）
        const size_t drop = dl - mant_digits;
        // 舍入位 = 第 (mant_digits+1) 位数字 = (work / 10^(drop−1)) 的个位
        // 低位粘性 = (work % 10^(drop−1)) != 0
        bigint_dec_ty hi;
        berr = bigint_dec_init(&hi);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&work);
            return BIGDECIMAL_ERR_OOM_E;
        }
        berr = bigint_dec_copy(&hi, &work);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&hi);
            bigint_dec_free(&work);
            return map_bigint_err(berr);
        }
        bigint_dec_div_pow10(&hi, drop - 1U);
        const unsigned round_digit = last_digit(&hi);
        // 低位粘性：hi × 10^(drop−1) 是否还原 work
        bigint_dec_mul_pow10(&hi, drop - 1U);
        const bool sticky_low = (bigint_dec_cmp(&hi, &work) != 0);
        bigint_dec_free(&hi);

        berr = bigint_dec_div_pow10(&work, drop);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&work);
            return map_bigint_err(berr);
        }
        exp += (int64_t)drop;

        const unsigned truncated_last = last_digit(&work);
        bool round_up = false;
        switch (ctx->round) {
            case BIGDECIMAL_ROUND_NEAREST_EVEN_E:
                round_up = (round_digit > 5U)
                        || ((round_digit == 5U)
                                && (sticky || sticky_low
                                        || ((truncated_last & 1U) != 0U)));
                break;
            case BIGDECIMAL_ROUND_TOWARD_ZERO_E:
                round_up = false;
                break;
            case BIGDECIMAL_ROUND_TOWARD_POS_E:
                round_up = (sign > 0)
                        && ((round_digit != 0U) || sticky || sticky_low);
                break;
            case BIGDECIMAL_ROUND_TOWARD_NEG_E:
                round_up = (sign < 0)
                        && ((round_digit != 0U) || sticky || sticky_low);
                break;
            case BIGDECIMAL_ROUND_AWAY_ZERO_E:
                round_up = (round_digit != 0U) || sticky || sticky_low;
                break;
            default:
                bigint_dec_free(&work);
                return BIGDECIMAL_ERR_INVALID_E;  // valid_ctx 已保证不可达
        }
        if (round_up) {
            bigint_dec_ty one;
            berr = bigint_dec_init(&one);
            if (berr != BIGINT_OK_E) {
                bigint_dec_free(&work);
                return BIGDECIMAL_ERR_OOM_E;
            }
            berr = bigint_dec_from_u64(&one, 1U);
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_add(&work, &work, &one);
            }
            bigint_dec_free(&one);
            if (berr != BIGINT_OK_E) {
                bigint_dec_free(&work);
                return map_bigint_err(berr);
            }
        }
    } else if (sticky) {
        // 防御：调用方契约要求不精确结果位数 > mant_digits（不可达）。
        // 定向模式按"最后一位上方存在极小量"进位一位；最近/向零未知
        // 且不影响精确舍入，保守不进位。
        bool up = false;
        switch (ctx->round) {
            case BIGDECIMAL_ROUND_TOWARD_POS_E:
                up = (sign > 0);
                break;
            case BIGDECIMAL_ROUND_TOWARD_NEG_E:
                up = (sign < 0);
                break;
            case BIGDECIMAL_ROUND_AWAY_ZERO_E:
                up = true;
                break;
            default:
                up = false;
                break;
        }
        if (up) {
            bigint_dec_ty one;
            berr = bigint_dec_init(&one);
            if (berr != BIGINT_OK_E) {
                bigint_dec_free(&work);
                return BIGDECIMAL_ERR_OOM_E;
            }
            berr = bigint_dec_from_u64(&one, 1U);
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_add(&work, &work, &one);
            }
            bigint_dec_free(&one);
            if (berr != BIGINT_OK_E) {
                bigint_dec_free(&work);
                return map_bigint_err(berr);
            }
        }
    }

    // 规范化：去除尾随零（含舍入进位到 10^mant_digits 的情形）
    exp += (int64_t)strip_trailing_zeros(&work);

    // 指数范围检查（§8.1）
    int64_t emin;
    int64_t emax;
    ctx_exp_range(ctx, &emin, &emax);
    if (exp > emax) {
        // 上溢：按舍入方向产生 ±∞ 或最大有限值
        bool to_inf = false;
        switch (ctx->round) {
            case BIGDECIMAL_ROUND_NEAREST_EVEN_E:
            case BIGDECIMAL_ROUND_AWAY_ZERO_E:
                to_inf = true;
                break;
            case BIGDECIMAL_ROUND_TOWARD_POS_E:
                to_inf = (sign > 0);
                break;
            case BIGDECIMAL_ROUND_TOWARD_NEG_E:
                to_inf = (sign < 0);
                break;
            default:
                to_inf = false;  // TOWARD_ZERO
                break;
        }
        if (to_inf) {
            bigint_dec_free(&work);
            set_special(dst, (sign < 0) ? BIGDECIMAL_NEG_INF_E
                    : BIGDECIMAL_POS_INF_E);
            return BIGDECIMAL_OK_E;
        }
        berr = max_finite_mant(&work, mant_digits);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&work);
            return map_bigint_err(berr);
        }
        exp = emax;
    } else if (exp < emin) {
        // 下溢：flush-to-zero。零符号（IEEE 754-2008 §7.5，设计 §7.3）：
        // 向 +∞ 舍入恒为 +0；其余模式取精确结果符号
        const bool to_neg_zero = (sign < 0)
                && (ctx->round != BIGDECIMAL_ROUND_TOWARD_POS_E);
        bigint_dec_free(&work);
        set_special(dst, to_neg_zero ? BIGDECIMAL_NEG_ZERO_E
                : BIGDECIMAL_POS_ZERO_E);
        return BIGDECIMAL_OK_E;
    }

    // 输出（接管 work，dst 与源别名安全：源已在 work 中独立）
    bigdecimal_free(dst);
    bigint_dec_move(&dst->mant, &work);
    dst->exp = exp;
    dst->flag = (sign < 0) ? BIGDECIMAL_NEG_E : BIGDECIMAL_POS_E;
    return BIGDECIMAL_OK_E;
}

/* ------------------------------------------------------------------ */
/* 十进制整数平方根（内部共享）                                        */
/* ------------------------------------------------------------------ */

/*
 * brief: 十进制逐位试商法整数平方根
 * note: 每轮消费 x 的两位、产出一位结果：
 *       rem = 100·rem + 两位；q = 10·q；试商 t = (20·q + d)·d（d 从 9
 *       向下试），若 t ≤ rem 则 rem −= t 且 q 末位置 d。
 *       正确性：q² = (10·q_old + d)² = 100·q_old² + (20·q_old + d)·d，
 *       rem 恒等于 x − q²×10^(2·剩余对数)（标准长除法式开方）。
 */
bigint_err_ty nex_dec_isqrt(bigint_dec_ty *q, bigint_dec_ty *rem,
        const bigint_dec_ty *x)
{
    if ((q == NULL) || (rem == NULL) || (x == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }

    /* 十进制数字串（处理数字对） */
    size_t need = 0;
    bigint_err_ty berr = bigint_dec_to_str(x, 10, NULL, 0, &need);
    if (berr != BIGINT_OK_E) {
        return berr;
    }
    char *digits = (char *)malloc(need + 1U);
    if (digits == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    berr = bigint_dec_to_str(x, 10, digits, need, &need);
    if (berr != BIGINT_OK_E) {
        free(digits);
        return berr;
    }
    const size_t dl = strlen(digits);
    if (dl == 0U || (dl == 1U && digits[0] == '0')) {
        free(digits);
        return BIGINT_OK_E;  // q = 0, rem = 0（调用方已初始化）
    }

    bigint_dec_ty ten;
    bigint_dec_ty twenty;
    bigint_dec_ty q20;
    bigint_dec_ty t;
    bigint_dec_ty td;
    bigint_dec_ty dsm;
    berr = bigint_dec_init(&ten);
    if (berr != BIGINT_OK_E) {
        free(digits);
        return berr;
    }
    berr = bigint_dec_init(&twenty);
    if (berr != BIGINT_OK_E) {
        free(digits);
        bigint_dec_free(&ten);
        return berr;
    }
    berr = bigint_dec_init(&q20);
    if (berr != BIGINT_OK_E) {
        free(digits);
        bigint_dec_free(&ten);
        bigint_dec_free(&twenty);
        return berr;
    }
    berr = bigint_dec_init(&t);
    if (berr != BIGINT_OK_E) {
        free(digits);
        bigint_dec_free(&ten);
        bigint_dec_free(&twenty);
        bigint_dec_free(&q20);
        return berr;
    }
    berr = bigint_dec_init(&td);
    if (berr != BIGINT_OK_E) {
        free(digits);
        bigint_dec_free(&ten);
        bigint_dec_free(&twenty);
        bigint_dec_free(&q20);
        bigint_dec_free(&t);
        return berr;
    }
    berr = bigint_dec_init(&dsm);
    if (berr != BIGINT_OK_E) {
        free(digits);
        bigint_dec_free(&ten);
        bigint_dec_free(&twenty);
        bigint_dec_free(&q20);
        bigint_dec_free(&t);
        bigint_dec_free(&td);
        return berr;
    }
    berr = bigint_dec_from_u64(&ten, 10U);
    if (berr == BIGINT_OK_E) {
        berr = bigint_dec_from_u64(&twenty, 20U);
    }

    /* 从最高位开始逐对处理（奇数位时首位为单数字） */
    size_t pos = 0U;
    if ((dl & 1U) != 0U) {
        // 首对为单数字
        const uint64_t pair = (uint64_t)(digits[0] - '0');
        pos = 1U;
        berr = bigint_dec_mul_pow10(rem, 2U);
        if (berr == BIGINT_OK_E) {
            bigint_dec_ty psm;
            berr = bigint_dec_init(&psm);
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_from_u64(&psm, pair);
                if (berr == BIGINT_OK_E) {
                    berr = bigint_dec_add(rem, rem, &psm);
                }
                bigint_dec_free(&psm);
            }
        }
        if (berr != BIGINT_OK_E) {
            goto isqrt_cleanup;
        }
        // 试商 d
        unsigned chosen = 0U;
        for (unsigned d = 9U; d > 0U; d--) {
            berr = bigint_dec_copy(&q20, q);
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_mul(&q20, &q20, &twenty);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_from_u64(&dsm, d);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_add(&t, &q20, &dsm);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_mul(&td, &t, &dsm);
            }
            if (berr != BIGINT_OK_E) {
                goto isqrt_cleanup;
            }
            if (bigint_dec_cmp(&td, rem) <= 0) {
                chosen = d;
                break;
            }
        }
        berr = bigint_dec_mul_pow10(q, 1U);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_from_u64(&dsm, chosen);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_add(q, q, &dsm);
        }
        if (berr == BIGINT_OK_E && chosen > 0U) {
            berr = bigint_dec_sub(rem, rem, &td);
        }
        if (berr != BIGINT_OK_E) {
            goto isqrt_cleanup;
        }
    }

    for (; pos + 1U < dl; pos += 2U) {
        const uint64_t pair = (uint64_t)(digits[pos] - '0') * 10U
                + (uint64_t)(digits[pos + 1U] - '0');
        berr = bigint_dec_mul_pow10(rem, 2U);
        if (berr == BIGINT_OK_E) {
            bigint_dec_ty psm;
            berr = bigint_dec_init(&psm);
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_from_u64(&psm, pair);
                if (berr == BIGINT_OK_E) {
                    berr = bigint_dec_add(rem, rem, &psm);
                }
                bigint_dec_free(&psm);
            }
        }
        if (berr != BIGINT_OK_E) {
            goto isqrt_cleanup;
        }
        unsigned chosen = 0U;
        for (unsigned d = 9U; d > 0U; d--) {
            berr = bigint_dec_copy(&q20, q);
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_mul(&q20, &q20, &twenty);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_from_u64(&dsm, d);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_add(&t, &q20, &dsm);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_dec_mul(&td, &t, &dsm);
            }
            if (berr != BIGINT_OK_E) {
                goto isqrt_cleanup;
            }
            if (bigint_dec_cmp(&td, rem) <= 0) {
                chosen = d;
                break;
            }
        }
        berr = bigint_dec_mul_pow10(q, 1U);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_from_u64(&dsm, chosen);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_add(q, q, &dsm);
        }
        if (berr == BIGINT_OK_E && chosen > 0U) {
            berr = bigint_dec_sub(rem, rem, &td);
        }
        if (berr != BIGINT_OK_E) {
            goto isqrt_cleanup;
        }
    }

isqrt_cleanup:
    free(digits);
    bigint_dec_free(&ten);
    bigint_dec_free(&twenty);
    bigint_dec_free(&q20);
    bigint_dec_free(&t);
    bigint_dec_free(&td);
    bigint_dec_free(&dsm);
    return berr;
}

/* ------------------------------------------------------------------ */
/* 生命周期与上下文（§8.2）                                            */
/* ------------------------------------------------------------------ */

bigdecimal_err_ty bigdecimal_init(bigdecimal_ty *val)
{
    if (val == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    val->flag = BIGDECIMAL_POS_ZERO_E;
    val->exp = 0;
    const bigint_err_ty berr = bigint_dec_init(&val->mant);
    if (berr != BIGINT_OK_E) {
        return map_bigint_err(berr);
    }
    return BIGDECIMAL_OK_E;
}

void bigdecimal_free(bigdecimal_ty *val)
{
    if (val == NULL) {
        return;
    }
    bigint_dec_free(&val->mant);
    val->exp = 0;
    val->flag = BIGDECIMAL_POS_ZERO_E;
}

bigdecimal_err_ty bigdecimal_copy(bigdecimal_ty *dst,
        const bigdecimal_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    return copy_value(dst, src);
}

bigdecimal_err_ty bigdecimal_ctx_make(bigdecimal_ctx_ty *dst,
        size_t mant_digits, size_t exp_digits, bigdecimal_round_ty round)
{
    if (dst == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if ((mant_digits < 1U) || (exp_digits < 1U)
            || (exp_digits > BIGDECIMAL_MAX_EXP_DIGITS)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if ((round < BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            || (round > BIGDECIMAL_ROUND_AWAY_ZERO_E)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    dst->mant_digits = mant_digits;
    dst->exp_digits = exp_digits;
    dst->round = round;
    return BIGDECIMAL_OK_E;
}

bigdecimal_ctx_ty bigdecimal_ctx_decimal32(void)
{
    bigdecimal_ctx_ty ctx;
    ctx.mant_digits = 7U;
    ctx.exp_digits = 2U;   // ±99 ≥ IEEE decimal32 的 emax 96
    ctx.round = BIGDECIMAL_ROUND_NEAREST_EVEN_E;
    return ctx;
}

bigdecimal_ctx_ty bigdecimal_ctx_decimal64(void)
{
    bigdecimal_ctx_ty ctx;
    ctx.mant_digits = 16U;
    ctx.exp_digits = 3U;   // ±999 ≥ IEEE decimal64 的 emax 384
    ctx.round = BIGDECIMAL_ROUND_NEAREST_EVEN_E;
    return ctx;
}

bigdecimal_ctx_ty bigdecimal_ctx_decimal128(void)
{
    bigdecimal_ctx_ty ctx;
    ctx.mant_digits = 34U;
    ctx.exp_digits = 4U;   // ±9999 ≥ IEEE decimal128 的 emax 6144
    ctx.round = BIGDECIMAL_ROUND_NEAREST_EVEN_E;
    return ctx;
}

/* ------------------------------------------------------------------ */
/* 分类断言（§8.2）                                                    */
/* ------------------------------------------------------------------ */

bool bigdecimal_is_zero(const bigdecimal_ty *val)
{
    if (val == NULL) {
        return false;
    }
    return (val->flag == BIGDECIMAL_POS_ZERO_E)
            || (val->flag == BIGDECIMAL_NEG_ZERO_E);
}

bool bigdecimal_is_inf(const bigdecimal_ty *val)
{
    if (val == NULL) {
        return false;
    }
    return (val->flag == BIGDECIMAL_POS_INF_E)
            || (val->flag == BIGDECIMAL_NEG_INF_E);
}

bool bigdecimal_is_nan(const bigdecimal_ty *val)
{
    return (val != NULL) && (val->flag == BIGDECIMAL_NAN_E);
}

bool bigdecimal_is_normal(const bigdecimal_ty *val)
{
    if (val == NULL) {
        return false;
    }
    return (val->flag == BIGDECIMAL_POS_E)
            || (val->flag == BIGDECIMAL_NEG_E);
}

/* ------------------------------------------------------------------ */
/* 构造与转换（§8.2）                                                  */
/* ------------------------------------------------------------------ */

bigdecimal_err_ty bigdecimal_from_bigint(bigdecimal_ty *dst,
        const bigint_dec_ty *src, const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    const int sign = (bigint_dec_sign(src) == BIGINT_SIGN_NEG_E) ? -1 : 1;
    return nex_dec_round_pack(dst, src, 0, false, sign, ctx);
}

/* ------------------------------------------------------------------ */
/* 算术（§8.2；特殊值传播见 §11）                                      */
/* ------------------------------------------------------------------ */

/*
 * brief: 常规加法/减法核心（两操作数均为正常值，非零非无穷非 NaN）
 * note: 按指数差用 mul_pow10 精确对齐（十进制移位 O(n)，§8.3），
 *       整数加减后一次舍入（见文件头注释）
 */
static bigdecimal_err_ty add_normal(bigdecimal_ty *dst,
        const bigdecimal_ty *lhs, const bigdecimal_ty *rhs, bool subtract,
        const bigdecimal_ctx_ty *ctx)
{
    const int64_t e1 = lhs->exp;
    const int64_t e2 = rhs->exp;
    const bool neg1 = (lhs->flag == BIGDECIMAL_NEG_E);
    const bool neg2 = ((rhs->flag == BIGDECIMAL_NEG_E) != subtract);

    const int64_t e_align = (e1 < e2) ? e1 : e2;

    bigint_dec_ty m1;
    bigint_dec_ty m2;
    bigint_dec_ty sum;
    bigint_err_ty berr;
    bigdecimal_err_ty ferr = BIGDECIMAL_OK_E;
    bigint_err_ty btmp = bigint_dec_init(&m1);
    if (btmp != BIGINT_OK_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&m2);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&m1);
        return BIGDECIMAL_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&sum);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&m1);
        bigint_dec_free(&m2);
        return BIGDECIMAL_ERR_OOM_E;
    }

    if (e1 > e_align) {
        berr = bigint_dec_copy(&m1, &lhs->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&m1, (size_t)(e1 - e_align));
        }
    } else {
        berr = bigint_dec_copy(&m1, &lhs->mant);
    }
    if (berr != BIGINT_OK_E) {
        ferr = map_bigint_err(berr);
        goto cleanup;
    }
    if (e2 > e_align) {
        berr = bigint_dec_copy(&m2, &rhs->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&m2, (size_t)(e2 - e_align));
        }
    } else {
        berr = bigint_dec_copy(&m2, &rhs->mant);
    }
    if (berr != BIGINT_OK_E) {
        ferr = map_bigint_err(berr);
        goto cleanup;
    }

    int sign = 1;
    if (neg1 == neg2) {
        berr = bigint_dec_add(&sum, &m1, &m2);
        if (berr != BIGINT_OK_E) {
            ferr = map_bigint_err(berr);
            goto cleanup;
        }
        sign = neg1 ? -1 : 1;
    } else {
        const int mag_cmp = bigint_dec_cmp_abs(&m1, &m2);
        if (mag_cmp > 0) {
            berr = bigint_dec_sub(&sum, &m1, &m2);
            if (berr != BIGINT_OK_E) {
                ferr = map_bigint_err(berr);
                goto cleanup;
            }
            sign = neg1 ? -1 : 1;
        } else if (mag_cmp < 0) {
            berr = bigint_dec_sub(&sum, &m2, &m1);
            if (berr != BIGINT_OK_E) {
                ferr = map_bigint_err(berr);
                goto cleanup;
            }
            sign = neg2 ? -1 : 1;
        } else {
            // 精确零：x + (−x)（含 x − x）→ 最近舍入 +0；向 −∞ 舍入 −0（§11）
            bigint_dec_free(&m1);
            bigint_dec_free(&m2);
            bigint_dec_free(&sum);
            set_special(dst, (ctx->round == BIGDECIMAL_ROUND_TOWARD_NEG_E)
                    ? BIGDECIMAL_NEG_ZERO_E : BIGDECIMAL_POS_ZERO_E);
            return BIGDECIMAL_OK_E;
        }
    }

    ferr = nex_dec_round_pack(dst, &sum, e_align, false, sign, ctx);
    if (ferr != BIGDECIMAL_OK_E) {
        goto cleanup;
    }

cleanup:
    bigint_dec_free(&m1);
    bigint_dec_free(&m2);
    bigint_dec_free(&sum);
    return ferr;
}

bigdecimal_err_ty bigdecimal_add(bigdecimal_ty *dst,
        const bigdecimal_ty *lhs, const bigdecimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(lhs) || bigdecimal_is_nan(rhs)) {
        set_special(dst, BIGDECIMAL_NAN_E);
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_inf(lhs) || bigdecimal_is_inf(rhs)) {
        if (bigdecimal_is_inf(lhs) && bigdecimal_is_inf(rhs)
                && (lhs->flag != rhs->flag)) {
            set_special(dst, BIGDECIMAL_NAN_E);  // +∞ + −∞
        } else {
            set_special(dst, bigdecimal_is_inf(lhs) ? lhs->flag : rhs->flag);
        }
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_zero(lhs) && bigdecimal_is_zero(rhs)) {
        if (lhs->flag == rhs->flag) {
            set_special(dst, lhs->flag);
        } else {
            set_special(dst, (ctx->round == BIGDECIMAL_ROUND_TOWARD_NEG_E)
                    ? BIGDECIMAL_NEG_ZERO_E : BIGDECIMAL_POS_ZERO_E);
        }
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_zero(lhs)) {
        return copy_value(dst, rhs);  // 0 + x = x
    }
    if (bigdecimal_is_zero(rhs)) {
        return copy_value(dst, lhs);  // x + 0 = x
    }
    return add_normal(dst, lhs, rhs, false, ctx);
}

bigdecimal_err_ty bigdecimal_sub(bigdecimal_ty *dst,
        const bigdecimal_ty *lhs, const bigdecimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(lhs) || bigdecimal_is_nan(rhs)) {
        set_special(dst, BIGDECIMAL_NAN_E);
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_inf(lhs) || bigdecimal_is_inf(rhs)) {
        if (bigdecimal_is_inf(lhs) && bigdecimal_is_inf(rhs)
                && (lhs->flag == rhs->flag)) {
            set_special(dst, BIGDECIMAL_NAN_E);  // +∞ − +∞
        } else if (bigdecimal_is_inf(lhs)) {
            set_special(dst, lhs->flag);  // ∞ − 有限 = ∞
        } else {
            set_special(dst, (rhs->flag == BIGDECIMAL_POS_INF_E)
                    ? BIGDECIMAL_NEG_INF_E : BIGDECIMAL_POS_INF_E);
        }
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_zero(lhs) && bigdecimal_is_zero(rhs)) {
        if (lhs->flag == rhs->flag) {
            set_special(dst, lhs->flag);
        } else {
            set_special(dst, (ctx->round == BIGDECIMAL_ROUND_TOWARD_NEG_E)
                    ? BIGDECIMAL_NEG_ZERO_E : BIGDECIMAL_POS_ZERO_E);
        }
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_zero(lhs)) {
        bigdecimal_err_ty err = copy_value(dst, rhs);  // 0 − x = −x
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_neg(dst);
        }
        return err;
    }
    if (bigdecimal_is_zero(rhs)) {
        return copy_value(dst, lhs);  // x − 0 = x
    }
    return add_normal(dst, lhs, rhs, true, ctx);
}

bigdecimal_err_ty bigdecimal_mul(bigdecimal_ty *dst,
        const bigdecimal_ty *lhs, const bigdecimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(lhs) || bigdecimal_is_nan(rhs)) {
        set_special(dst, BIGDECIMAL_NAN_E);
        return BIGDECIMAL_OK_E;
    }
    const bool neg = flag_is_negative(lhs->flag)
            != flag_is_negative(rhs->flag);
    if (bigdecimal_is_inf(lhs) || bigdecimal_is_inf(rhs)) {
        if (bigdecimal_is_zero(lhs) || bigdecimal_is_zero(rhs)) {
            set_special(dst, BIGDECIMAL_NAN_E);  // 0 × ∞
        } else {
            set_special(dst, neg ? BIGDECIMAL_NEG_INF_E
                    : BIGDECIMAL_POS_INF_E);
        }
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_zero(lhs) || bigdecimal_is_zero(rhs)) {
        set_special(dst, neg ? BIGDECIMAL_NEG_ZERO_E
                : BIGDECIMAL_POS_ZERO_E);
        return BIGDECIMAL_OK_E;
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

    bigint_dec_ty prod;
    bigint_err_ty berr = bigint_dec_init(&prod);
    if (berr != BIGINT_OK_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    berr = bigint_dec_mul(&prod, &lhs->mant, &rhs->mant);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&prod);
        return map_bigint_err(berr);
    }

    bigdecimal_err_ty ferr;
    if (exp_overflow) {
        int64_t emax;
        int64_t emin;
        ctx_exp_range(ctx, &emin, &emax);
        ferr = nex_dec_round_pack(dst, &prod, emax + 1, false,
                neg ? -1 : 1, ctx);
    } else if (exp_underflow) {
        int64_t emax;
        int64_t emin;
        ctx_exp_range(ctx, &emin, &emax);
        ferr = nex_dec_round_pack(dst, &prod, emin - 1, false,
                neg ? -1 : 1, ctx);
    } else {
        ferr = nex_dec_round_pack(dst, &prod, raw_exp, false,
                neg ? -1 : 1, ctx);
    }
    bigint_dec_free(&prod);
    return ferr;
}

bigdecimal_err_ty bigdecimal_div(bigdecimal_ty *dst,
        const bigdecimal_ty *lhs, const bigdecimal_ty *rhs,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(lhs) || bigdecimal_is_nan(rhs)) {
        set_special(dst, BIGDECIMAL_NAN_E);
        return BIGDECIMAL_OK_E;
    }
    const bool neg = flag_is_negative(lhs->flag)
            != flag_is_negative(rhs->flag);
    const bool lhs_zero = bigdecimal_is_zero(lhs);
    const bool rhs_zero = bigdecimal_is_zero(rhs);
    if (rhs_zero) {
        set_special(dst, lhs_zero ? BIGDECIMAL_NAN_E
                : (neg ? BIGDECIMAL_NEG_INF_E : BIGDECIMAL_POS_INF_E));
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_inf(lhs) || bigdecimal_is_inf(rhs)) {
        if (bigdecimal_is_inf(lhs) && bigdecimal_is_inf(rhs)) {
            set_special(dst, BIGDECIMAL_NAN_E);  // ∞ / ∞
        } else if (bigdecimal_is_inf(lhs)) {
            set_special(dst, neg ? BIGDECIMAL_NEG_INF_E
                    : BIGDECIMAL_POS_INF_E);
        } else {
            set_special(dst, neg ? BIGDECIMAL_NEG_ZERO_E
                    : BIGDECIMAL_POS_ZERO_E);
        }
        return BIGDECIMAL_OK_E;
    }
    if (lhs_zero) {
        set_special(dst, neg ? BIGDECIMAL_NEG_ZERO_E
                : BIGDECIMAL_POS_ZERO_E);
        return BIGDECIMAL_OK_E;
    }

    // 常规：被除数/除数缩放使商位长 = mant_digits + 1..2（含保护位，正确舍入）
    const int64_t e1 = lhs->exp;
    const int64_t e2 = rhs->exp;
    const size_t dl1 = bigint_dec_digit_len(&lhs->mant);
    const size_t dl2 = bigint_dec_digit_len(&rhs->mant);
    // shift = 目标商位长 − 自然商位长 = (mant_digits + 1) − (dl1 − dl2 + 1)
    const int64_t shift = (int64_t)ctx->mant_digits + 1 - (int64_t)dl1
            + (int64_t)dl2;

    bigint_dec_ty num;
    bigint_dec_ty den;
    bigint_dec_ty quot;
    bigint_dec_ty rem;
    bigint_err_ty berr;
    bigint_err_ty btmp = bigint_dec_init(&num);
    if (btmp != BIGINT_OK_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&den);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&num);
        return BIGDECIMAL_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&quot);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&num);
        bigint_dec_free(&den);
        return BIGDECIMAL_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&rem);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&num);
        bigint_dec_free(&den);
        bigint_dec_free(&quot);
        return BIGDECIMAL_ERR_OOM_E;
    }

    if (shift >= 0) {
        berr = bigint_dec_copy(&num, &lhs->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&num, (size_t)shift);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_copy(&den, &rhs->mant);
        }
    } else {
        berr = bigint_dec_copy(&num, &lhs->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_copy(&den, &rhs->mant);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&den, (size_t)(-shift));
        }
    }
    if (berr != BIGINT_OK_E) {
        goto div_cleanup;
    }
    berr = bigint_dec_div_rem(&quot, &rem, &num, &den);
    if (berr != BIGINT_OK_E) {
        goto div_cleanup;
    }
    {
        // 余数非零 → 商严格大于整数部分 → 粘位（商位长 > mant_digits，
        // 满足 round_pack 的 sticky 契约）
        const bool sticky = !bigint_dec_is_zero(&rem);
        // raw_exp = e1 − e2 − shift（shift 已并入 num/den 缩放）；
        // 分步防溢出：|shift| ≤ mant_digits + dl2 + 1 << 内存限制，
        // 无法抵消 e1 − e2 的 int64 级溢出 → 溢出即上溢/下溢
        int64_t raw_exp = 0;
        bool exp_overflow = false;
        bool exp_underflow = false;
        if ((e1 > 0) && (e2 < 0) && (e1 > INT64_MAX + e2)) {
            exp_overflow = true;
        } else if ((e1 < 0) && (e2 > 0) && (e1 < INT64_MIN + e2)) {
            exp_underflow = true;
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

        bigdecimal_err_ty ferr;
        if (exp_overflow) {
            int64_t emax;
            int64_t emin;
            ctx_exp_range(ctx, &emin, &emax);
            ferr = nex_dec_round_pack(dst, &quot, emax + 1, false,
                    neg ? -1 : 1, ctx);
        } else if (exp_underflow) {
            int64_t emax;
            int64_t emin;
            ctx_exp_range(ctx, &emin, &emax);
            ferr = nex_dec_round_pack(dst, &quot, emin - 1, sticky,
                    neg ? -1 : 1, ctx);
        } else {
            ferr = nex_dec_round_pack(dst, &quot, raw_exp, sticky,
                    neg ? -1 : 1, ctx);
        }
        berr = (ferr == BIGDECIMAL_OK_E) ? BIGINT_OK_E : BIGINT_ERR_INVALID_E;
    }

div_cleanup:
    bigint_dec_free(&num);
    bigint_dec_free(&den);
    bigint_dec_free(&quot);
    bigint_dec_free(&rem);
    return map_bigint_err(berr);
}

bigdecimal_err_ty bigdecimal_sqrt(bigdecimal_ty *dst,
        const bigdecimal_ty *src, const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(src)) {
        set_special(dst, BIGDECIMAL_NAN_E);
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_inf(src)) {
        set_special(dst, (src->flag == BIGDECIMAL_NEG_INF_E)
                ? BIGDECIMAL_NAN_E : BIGDECIMAL_POS_INF_E);  // √−∞ = NaN
        return BIGDECIMAL_OK_E;
    }
    if (bigdecimal_is_zero(src)) {
        set_special(dst, src->flag);  // √±0 = ±0
        return BIGDECIMAL_OK_E;
    }
    if (src->flag == BIGDECIMAL_NEG_E) {
        set_special(dst, BIGDECIMAL_NAN_E);  // 负数 → NaN
        return BIGDECIMAL_OK_E;
    }

    // 常规：值 = mant × 10^exp；先把指数调整为偶数（Y × 10^(2k) 形式）
    bigint_dec_ty y;
    bigint_err_ty berr = bigint_dec_init(&y);
    if (berr != BIGINT_OK_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    int64_t e_adj;
    if ((src->exp & 1) != 0) {
        // exp 为奇：Y = mant × 10，e_adj = (exp − 1) / 2
        berr = bigint_dec_copy(&y, &src->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&y, 1U);
        }
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&y);
            return map_bigint_err(berr);
        }
        e_adj = (src->exp - 1) / 2;
    } else {
        berr = bigint_dec_copy(&y, &src->mant);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&y);
            return map_bigint_err(berr);
        }
        e_adj = src->exp / 2;
    }

    // 缩放 Y 使 sqrt 位长 = mant_digits + 2（含保护位）
    const size_t target = 2U * (ctx->mant_digits + 2U);
    const size_t dl_y = bigint_dec_digit_len(&y);
    int64_t scale_shift = 0;
    bool shifted_out = false;
    if (dl_y > target) {
        // 右移偶数位（保留 sqrt 的整数性），移出位 → 粘位
        size_t r;
        if (((dl_y - target) & 1U) == 0U) {
            r = dl_y - target;
        } else {
            r = dl_y - (target - 1U);
        }
        // 移出位是否非零：右移后乘回比较
        bigint_dec_ty t;
        berr = bigint_dec_init(&t);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&y);
            return BIGDECIMAL_ERR_OOM_E;
        }
        berr = bigint_dec_copy(&t, &y);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_div_pow10(&t, r);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&t, r);
        }
        if (berr == BIGINT_OK_E) {
            shifted_out = (bigint_dec_cmp(&t, &y) != 0);
        }
        bigint_dec_free(&t);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&y);
            return map_bigint_err(berr);
        }
        berr = bigint_dec_div_pow10(&y, r);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&y);
            return map_bigint_err(berr);
        }
        // y' = y / 10^r，q = floor(sqrt(y')) ≈ sqrt(y)·10^(−r/2)；
        // sqrt(value) = sqrt(y)·10^e_adj = q·10^(e_adj + r/2) → scale_shift = +r/2
        scale_shift = (int64_t)(r / 2U);
    } else if (dl_y < target - 1U) {
        // 左移偶数位使位长 ∈ {target−1, target}
        size_t l;
        if (((target - dl_y) & 1U) == 0U) {
            l = target - dl_y;
        } else {
            l = target - 1U - dl_y;
        }
        berr = bigint_dec_mul_pow10(&y, l);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&y);
            return map_bigint_err(berr);
        }
        // y' = y × 10^l，q = floor(sqrt(y')) ≈ sqrt(y)·10^(l/2)；
        // sqrt(value) = sqrt(y)·10^e_adj = q·10^(e_adj − l/2) → scale_shift = −l/2
        scale_shift = -(int64_t)(l / 2U);
    }

    // 整数平方根：q = floor(sqrt(Y))，rem = Y − q²
    bigint_dec_ty q;
    bigint_dec_ty rem;
    berr = bigint_dec_init(&q);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&y);
        return BIGDECIMAL_ERR_OOM_E;
    }
    berr = bigint_dec_init(&rem);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&y);
        bigint_dec_free(&q);
        return BIGDECIMAL_ERR_OOM_E;
    }
    berr = nex_dec_isqrt(&q, &rem, &y);
    bigint_dec_free(&y);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&q);
        bigint_dec_free(&rem);
        return map_bigint_err(berr);
    }

    // 余数非零 → 平方根严格大于整数部分 → 粘位（q 位长 = p + 2）
    const bool sticky = !bigint_dec_is_zero(&rem) || shifted_out;
    const int64_t raw_exp = e_adj + scale_shift;
    const bigdecimal_err_ty ferr = nex_dec_round_pack(dst, &q, raw_exp,
            sticky, 1, ctx);
    bigint_dec_free(&q);
    bigint_dec_free(&rem);
    return ferr;
}

bigdecimal_err_ty bigdecimal_neg(bigdecimal_ty *val)
{
    if (val == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    switch (val->flag) {
        case BIGDECIMAL_POS_ZERO_E:
            val->flag = BIGDECIMAL_NEG_ZERO_E;
            break;
        case BIGDECIMAL_NEG_ZERO_E:
            val->flag = BIGDECIMAL_POS_ZERO_E;
            break;
        case BIGDECIMAL_POS_E:
            val->flag = BIGDECIMAL_NEG_E;
            break;
        case BIGDECIMAL_NEG_E:
            val->flag = BIGDECIMAL_POS_E;
            break;
        case BIGDECIMAL_POS_INF_E:
            val->flag = BIGDECIMAL_NEG_INF_E;
            break;
        case BIGDECIMAL_NEG_INF_E:
            val->flag = BIGDECIMAL_POS_INF_E;
            break;
        case BIGDECIMAL_NAN_E:
            break;  // NaN 不变
        default:
            return BIGDECIMAL_ERR_INVALID_E;  // 防御：非法标志
    }
    return BIGDECIMAL_OK_E;
}

/* ------------------------------------------------------------------ */
/* 比较（§8.2）                                                        */
/* ------------------------------------------------------------------ */

/*
 * brief: 比较两个 (幅值, 指数) 对 |m1| × 10^e1 与 |m2| × 10^e2
 * note: 用"位长区间"快速判定（区间不重叠时免分配），重叠时对齐精确比较；
 *       重叠时 |e1−e2| < max(位数)+1，对齐移位有界
 */
static int cmp_mag_exp(const bigint_dec_ty *m1, int64_t e1,
        const bigint_dec_ty *m2, int64_t e2)
{
    if (e1 == e2) {
        return bigint_dec_cmp(m1, m2);
    }
    const int64_t dl1 = (int64_t)bigint_dec_digit_len(m1);
    const int64_t dl2 = (int64_t)bigint_dec_digit_len(m2);
    // 值域：m1×10^e1 ∈ [10^(dl1−1+e1), 10^(dl1+e1))（e、dl 均受 int64 约束）
    const int64_t lo1 = dl1 - 1 + e1;
    const int64_t hi1 = dl1 + e1;
    const int64_t lo2 = dl2 - 1 + e2;
    const int64_t hi2 = dl2 + e2;
    if (hi1 <= lo2) {
        return -1;
    }
    if (hi2 <= lo1) {
        return 1;
    }

    // 重叠：精确比较（对齐指数；d 有界）。
    // 推导：e1 > e2 时 d = e1 − e2，两边同除 10^e2 → 比较 m1×10^d 与 m2；
    // e2 > e1 时对称比较 m1 与 m2×10^d。
    bigint_dec_ty tmp;
    bigint_err_ty berr = bigint_dec_init(&tmp);
    if (berr != BIGINT_OK_E) {
        return 0;  // OOM 防御
    }
    int result = 0;
    if (e1 > e2) {
        const uint64_t d = (uint64_t)(e1 - e2);
        berr = bigint_dec_copy(&tmp, m1);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&tmp, (size_t)d);
        }
        if (berr == BIGINT_OK_E) {
            result = bigint_dec_cmp(&tmp, m2);
        }
    } else {
        const uint64_t d = (uint64_t)(e2 - e1);
        berr = bigint_dec_copy(&tmp, m2);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul_pow10(&tmp, (size_t)d);
        }
        if (berr == BIGINT_OK_E) {
            result = bigint_dec_cmp(m1, &tmp);
        }
    }
    bigint_dec_free(&tmp);
    return result;
}

int bigdecimal_cmp(const bigdecimal_ty *lhs, const bigdecimal_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return 2;  // 保守：视为 NaN 语义
    }
    if (bigdecimal_is_nan(lhs) || bigdecimal_is_nan(rhs)) {
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
    const int mag_cmp = cmp_mag_exp(&lhs->mant, lhs->exp,
            &rhs->mant, rhs->exp);
    if (lhs->flag == BIGDECIMAL_NEG_E) {
        return -mag_cmp;  // 负数：幅值大者反而小
    }
    return mag_cmp;
}

bool bigdecimal_eq(const bigdecimal_ty *lhs, const bigdecimal_ty *rhs)
{
    return bigdecimal_cmp(lhs, rhs) == 0;
}

/* ------------------------------------------------------------------ */
/* 分解与合成（§8.2）                                                  */
/* ------------------------------------------------------------------ */

bigdecimal_err_ty bigdecimal_decompose(const bigdecimal_ty *src,
        bigint_dec_ty *mant, int64_t *exp, bigdecimal_flag_ty *flag)
{
    if ((src == NULL) || (mant == NULL)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    const bigint_err_ty berr = bigint_dec_copy(mant, &src->mant);
    if (berr != BIGINT_OK_E) {
        return map_bigint_err(berr);
    }
    if (exp != NULL) {
        *exp = src->exp;
    }
    if (flag != NULL) {
        *flag = src->flag;
    }
    return BIGDECIMAL_OK_E;
}

bigdecimal_err_ty bigdecimal_compose(bigdecimal_ty *dst,
        const bigint_dec_ty *mant, int64_t exp, bigdecimal_flag_ty flag,
        const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (mant == NULL) || !valid_ctx(ctx)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    switch (flag) {
        case BIGDECIMAL_POS_ZERO_E:
        case BIGDECIMAL_NEG_ZERO_E:
        case BIGDECIMAL_POS_INF_E:
        case BIGDECIMAL_NEG_INF_E:
        case BIGDECIMAL_NAN_E:
            set_special(dst, flag);  // 特殊值忽略 mant / exp
            return BIGDECIMAL_OK_E;
        case BIGDECIMAL_POS_E:
        case BIGDECIMAL_NEG_E:
            return nex_dec_round_pack(dst, mant, exp, false,
                    (flag == BIGDECIMAL_NEG_E) ? -1 : 1, ctx);
        default:
            return BIGDECIMAL_ERR_INVALID_E;  // 非法标志
    }
}
