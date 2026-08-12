/*
 * nex_bigint_conv.c：bigint_bin ↔ bigint_dec 互转（设计文档 §4.2.6、§4.3）。
 *
 * 本单元是唯一同时包含 bin 与 dec 头文件的模块（§2.1 的唯一跨支线依赖）。
 *
 * 算法（§13 #10 分治基数转换落地）：
 *   - 小规模（len ≤ 阈值）：朴素 O(n²)——bin→dec 反复除以 10^9（余数即
 *     十进制肢）；dec→bin 逐肢 "乘 10^9 加当前肢"；
 *   - 大规模：分治。bin→dec 按 2 的幂肢界切半 v = hi·2^(32m) + lo，
 *     递归转 hi/lo 后 hi_dec·pow2[k] + lo_dec 合并；dec→bin 对称，
 *     切半 v = hi·10^(9m) + lo 后 hi_bin·pow10[k] + lo_bin 合并。
 *     pow2[k] = 2^(32·2^k)（十进制）与 pow10[k] = 10^(9·2^k)（二进制）
 *     各以平方链一次构建、全递归共享。复杂度 T(n) = 2T(n/2) + M(n)，
 *     随乘法为 O(n^1.585)（Karatsuba）。
 *
 * 两侧输出均保持规范化表示：最高有效肢非零，零 ⇔ len == 0 且 sign == ZERO。
 * 中间量一律经 uint64_t 显式计算（§4.3、§5.3），不依赖编译器溢出行为。
 * 本模块无全局状态（§3.3），分治表按调用构建、栈上存放结构体。
 */

#include "nex/bigint/nex_bigint_conv.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

/* 十进制肢基 10^9（小于 2^32，可作 bin 上的单肢乘数 / 除数） */
#define NEX_DEC_BASE UINT32_C(1000000000)

/* 分治切换阈值（按方向独立，实测标定，见 bench）：
 * - bin→dec（输入 bin 肢）：256 起分治胜出（8192 肢快 3.6 倍）；
 * - dec→bin（输入 dec 肢）：朴素 Horner（乘加）开销低，2048 起分治才胜出
 *   （8192 肢快 2 倍） */
#define NEX_CONV_B2D_THRESHOLD 256U
#define NEX_CONV_D2B_THRESHOLD 2048U

/* 分治表容量：覆盖 len ≤ 2^59 肢（2^64 bit）的转换，理论不可越界 */
#define NEX_CONV_TABLE_MAX 68U

/* ------------------------------------------------------------------ */
/* 内部辅助：容量（自包含，不依赖 bin.c / dec.c 的 static 函数）            */
/* ------------------------------------------------------------------ */

/*
 * brief: 确保 dec 对象容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty conv_dec_ensure(bigint_dec_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)nex_realloc(val->limbs,
            new_cap * sizeof(uint32_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

/*
 * brief: 确保 bin 对象容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty conv_bin_ensure(bigint_bin_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)nex_realloc(val->limbs,
            new_cap * sizeof(uint32_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：朴素转换（分治的基例）                                     */
/* ------------------------------------------------------------------ */

/*
 * brief: bin 幅值就地乘 10^9 再加小整数：|val| = |val| × 10^9 + addend
 * param: addend 加数，小于 10^9（即源 dec 肢）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 * note: 乘积累加 ≤ (2^32−1) × 10^9 + (10^9−1) < 2^62，uint64_t 安全；
 *       失败时低位肢可能已改写，调用方须放弃该对象（仅用于临时量）
 */
static bigint_err_ty bin_mul_add_base(bigint_bin_ty *val, uint32_t addend)
{
    uint64_t carry = addend;
    for (size_t idx = 0U; idx < val->len; idx++) {
        const uint64_t cur = (uint64_t)val->limbs[idx] * (uint64_t)NEX_DEC_BASE
                + carry;
        val->limbs[idx] = (uint32_t)cur;
        carry = cur >> 32U;
    }
    while (carry > 0U) {
        const bigint_err_ty err = conv_bin_ensure(val, val->len + 1U);
        if (err != BIGINT_OK_E) {
            return err;
        }
        val->limbs[val->len] = (uint32_t)carry;
        carry >>= 32U;
        val->len++;
    }
    return BIGINT_OK_E;
}

/*
 * brief: bin 肢视图朴素转 dec：反复除以 10^9，余数即十进制肢
 * param: out    已初始化（len = 0）的目标，输出按低位肢在前追加
 * param: limbs  视图起点（len ≥ 1，最高肢非零）
 */
static bigint_err_ty conv_b2d_naive(bigint_dec_ty *out,
        const uint32_t *limbs, size_t len)
{
    uint32_t *work = (uint32_t *)nex_malloc(len * sizeof(uint32_t));
    if (work == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    memcpy(work, limbs, len * sizeof(uint32_t));
    size_t work_len = len;

    bigint_err_ty err = BIGINT_OK_E;
    while (work_len > 0U) {
        /* 单肢除法：cur ≤ (10^9−1)·2^32 + (2^32−1) < 2^62 */
        uint64_t rem = 0U;
        for (size_t idx = work_len; idx-- > 0U;) {
            const uint64_t cur = (rem << 32U) | work[idx];
            work[idx] = (uint32_t)(cur / (uint64_t)NEX_DEC_BASE);
            rem = cur % (uint64_t)NEX_DEC_BASE;
        }
        while ((work_len > 0U) && (work[work_len - 1U] == 0U)) {
            work_len--;
        }
        err = conv_dec_ensure(out, out->len + 1U);
        if (err != BIGINT_OK_E) {
            break;
        }
        out->limbs[out->len] = (uint32_t)rem;
        out->len++;
    }
    if (err == BIGINT_OK_E) {
        /* 幅值结果须设符号：bigint_dec_add 按 sign == ZERO 视为零 */
        out->sign = (out->len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    }
    free(work);
    return err;
}

/*
 * brief: dec 肢视图朴素转 bin：自高向低 "乘 10^9 加当前肢"
 * param: out    已初始化（len = 0）的目标
 */
static bigint_err_ty conv_d2b_naive(bigint_bin_ty *out,
        const uint32_t *limbs, size_t len)
{
    bigint_err_ty err = BIGINT_OK_E;
    for (size_t idx = len; idx-- > 0U;) {
        err = bin_mul_add_base(out, limbs[idx]);
        if (err != BIGINT_OK_E) {
            break;
        }
    }
    if (err == BIGINT_OK_E) {
        /* 幅值结果须设符号：bigint_bin_add 按 sign == ZERO 视为零 */
        out->sign = (out->len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：分治转换（设计文档 §13 #10）                               */
/* ------------------------------------------------------------------ */

/*
 * brief: 构建 2^(32·2^k)（k = 0..j_max）的十进制表：pow2[0] = 2^32，
 *        之后平方链 pow2[j] = pow2[j−1]²
 * param: len    bin 幅值肢数（决定所需 j_max）
 */
static bigint_err_ty conv_b2d_build_table(bigint_dec_ty *pow2, size_t len)
{
    /* 最高切分 m = 2^k ≤ len/2 → 需 pow2[k]；多建 1 项保险 */
    uint32_t j_max = 0U;
    size_t t = len >> 1U;
    while (t > 0U) {
        t >>= 1U;
        j_max++;
    }
    if (j_max >= NEX_CONV_TABLE_MAX) {
        return BIGINT_ERR_UNSUPPORTED_E;  /* len > 2^68 肢，理论不可达 */
    }

    bigint_err_ty err = bigint_dec_from_u64(&pow2[0], UINT64_C(1) << 32);
    if (err != BIGINT_OK_E) {
        return err;
    }
    for (uint32_t j = 1U; j <= j_max; j++) {
        err = bigint_dec_mul(&pow2[j], &pow2[j - 1U], &pow2[j - 1U]);
        if (err != BIGINT_OK_E) {
            return err;
        }
    }
    return BIGINT_OK_E;
}

/*
 * brief: 构建 10^(9·2^k)（k = 0..j_max）的二进制表：pow10[0] = 10^9，
 *        之后平方链 pow10[j] = pow10[j−1]²
 */
static bigint_err_ty conv_d2b_build_table(bigint_bin_ty *pow10, size_t len)
{
    uint32_t j_max = 0U;
    size_t t = len >> 1U;
    while (t > 0U) {
        t >>= 1U;
        j_max++;
    }
    if (j_max >= NEX_CONV_TABLE_MAX) {
        return BIGINT_ERR_UNSUPPORTED_E;  /* len > 2^68 肢，理论不可达 */
    }

    bigint_err_ty err = bigint_bin_from_u64(&pow10[0], (uint64_t)NEX_DEC_BASE);
    if (err != BIGINT_OK_E) {
        return err;
    }
    for (uint32_t j = 1U; j <= j_max; j++) {
        err = bigint_bin_mul(&pow10[j], &pow10[j - 1U], &pow10[j - 1U]);
        if (err != BIGINT_OK_E) {
            return err;
        }
    }
    return BIGINT_OK_E;
}

/*
 * brief: 分治 bin→dec：v = hi·2^(32m) + lo（m = 2^k 为 ≤ len/2 的最大
 *        2 的幂），结果 = hi_dec·pow2[k+5] + lo_dec
 * param: limbs  视图起点（最高肢非零）
 * param: pow2   十进制 2^(32·2^k) 表（见 conv_b2d_build_table）
 */
static bigint_err_ty conv_b2d_dc(bigint_dec_ty *out, const uint32_t *limbs,
        size_t len, const bigint_dec_ty *pow2)
{
    if (len <= NEX_CONV_B2D_THRESHOLD) {
        return conv_b2d_naive(out, limbs, len);
    }

    size_t m = 1U;
    uint32_t k = 0U;
    while ((m << 1U) <= (len >> 1U)) {
        m <<= 1U;
        k++;
    }

    bigint_dec_ty lo_dec;
    bigint_dec_ty hi_dec;
    bigint_dec_ty prod;
    (void)bigint_dec_init(&lo_dec);  // 栈对象非空，恒成功
    (void)bigint_dec_init(&hi_dec);
    (void)bigint_dec_init(&prod);

    bigint_err_ty err = conv_b2d_dc(&lo_dec, limbs, m, pow2);
    if (err == BIGINT_OK_E) {
        err = conv_b2d_dc(&hi_dec, limbs + m, len - m, pow2);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_dec_mul(&prod, &hi_dec, &pow2[k]);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_dec_add(out, &prod, &lo_dec);
    }

    bigint_dec_free(&prod);
    bigint_dec_free(&hi_dec);
    bigint_dec_free(&lo_dec);
    return err;
}

/*
 * brief: 分治 dec→bin：v = hi·10^(9m) + lo（m = 2^k 为 ≤ len/2 的最大
 *        2 的幂），结果 = hi_bin·pow10[k] + lo_bin
 */
static bigint_err_ty conv_d2b_dc(bigint_bin_ty *out, const uint32_t *limbs,
        size_t len, const bigint_bin_ty *pow10)
{
    if (len <= NEX_CONV_D2B_THRESHOLD) {
        return conv_d2b_naive(out, limbs, len);
    }

    size_t m = 1U;
    uint32_t k = 0U;
    while ((m << 1U) <= (len >> 1U)) {
        m <<= 1U;
        k++;
    }

    bigint_bin_ty lo_bin;
    bigint_bin_ty hi_bin;
    bigint_bin_ty prod;
    (void)bigint_bin_init(&lo_bin);  // 栈对象非空，恒成功
    (void)bigint_bin_init(&hi_bin);
    (void)bigint_bin_init(&prod);

    bigint_err_ty err = conv_d2b_dc(&lo_bin, limbs, m, pow10);
    if (err == BIGINT_OK_E) {
        err = conv_d2b_dc(&hi_bin, limbs + m, len - m, pow10);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul(&prod, &hi_bin, &pow10[k]);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(out, &prod, &lo_bin);
    }

    bigint_bin_free(&prod);
    bigint_bin_free(&hi_bin);
    bigint_bin_free(&lo_bin);
    return err;
}

/* ------------------------------------------------------------------ */
/* 对外 API                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: bin 转 dec（分治；小规模回退朴素反复除 10^9）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 结果天然规范化（最高有效肢非零）；分治表一次构建全递归共享
 */
bigint_err_ty bigint_conv_bin_to_dec(bigint_dec_ty *dst,
        const bigint_bin_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (src->len == 0U) {
        return bigint_dec_from_u64(dst, 0U);
    }

    bigint_dec_ty tmp;
    (void)bigint_dec_init(&tmp);  // 栈对象非空，恒成功
    bigint_err_ty err = BIGINT_OK_E;
    if (src->len <= NEX_CONV_B2D_THRESHOLD) {
        err = conv_b2d_naive(&tmp, src->limbs, src->len);
    } else {
        bigint_dec_ty pow2[NEX_CONV_TABLE_MAX];
        for (uint32_t j = 0U; j < NEX_CONV_TABLE_MAX; j++) {
            (void)bigint_dec_init(&pow2[j]);
        }
        err = conv_b2d_build_table(pow2, src->len);
        if (err == BIGINT_OK_E) {
            err = conv_b2d_dc(&tmp, src->limbs, src->len, pow2);
        }
        for (uint32_t j = 0U; j < NEX_CONV_TABLE_MAX; j++) {
            bigint_dec_free(&pow2[j]);
        }
    }
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&tmp);
        return err;
    }

    tmp.sign = src->sign;  // src 非零，tmp.len 恒 > 0
    bigint_dec_move(dst, &tmp);
    return BIGINT_OK_E;
}

/*
 * brief: dec 转 bin（分治；小规模回退朴素逐肢乘 10^9）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_conv_dec_to_bin(bigint_bin_ty *dst,
        const bigint_dec_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (src->len == 0U) {
        return bigint_bin_from_u64(dst, 0U);
    }

    bigint_bin_ty tmp;
    (void)bigint_bin_init(&tmp);  // 栈对象非空，恒成功
    bigint_err_ty err = BIGINT_OK_E;
    if (src->len <= NEX_CONV_D2B_THRESHOLD) {
        err = conv_d2b_naive(&tmp, src->limbs, src->len);
    } else {
        bigint_bin_ty pow10[NEX_CONV_TABLE_MAX];
        for (uint32_t j = 0U; j < NEX_CONV_TABLE_MAX; j++) {
            (void)bigint_bin_init(&pow10[j]);
        }
        err = conv_d2b_build_table(pow10, src->len);
        if (err == BIGINT_OK_E) {
            err = conv_d2b_dc(&tmp, src->limbs, src->len, pow10);
        }
        for (uint32_t j = 0U; j < NEX_CONV_TABLE_MAX; j++) {
            bigint_bin_free(&pow10[j]);
        }
    }
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp);
        return err;
    }

    tmp.sign = src->sign;  // src 非零，tmp.len 恒 > 0
    bigint_bin_move(dst, &tmp);
    return BIGINT_OK_E;
}
