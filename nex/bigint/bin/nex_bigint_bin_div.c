/*
 * nex_bigint_bin_div.c：bigint_bin_ty 除法与幂实现。
 *
 * 职责（设计文档 §4.2.4、§4.3）：带余除法（Knuth《TAOCP》卷 2 Algorithm D，
 * 规范化 + 试商修正）、幂（square-and-multiply）、模幂（模平方-乘）。
 *
 * 除法语义：截断除法，与 C99 整数除法一致——商向零取整，余数符号与被除数
 * 相同（lhs = quot × rhs + rem，|rem| < |rhs|）。
 *
 * 规范化不变式、别名约定与"失败时输出不变"契约同头文件。本文件仅处理
 * 幅值内部的中间量，符号由顶层函数统一设置；乘 / 减等复用公开 API。
 */

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

#ifdef NEX_DIV_DEBUG
#include <stdio.h>
#endif

/* 基 2^32 的 2^32 常量，用于 Algorithm D 的试商比较 */
#define NEX_BASE_U64 (UINT64_C(1) << 32U)

/* ------------------------------------------------------------------ */
/* 内部辅助：容量与规范化（算法文件自包含，不依赖 bin.c 的 static 函数）      */
/* ------------------------------------------------------------------ */

/*
 * brief: 确保 val 的容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty div_ensure_cap(bigint_bin_ty *val, size_t needed)
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
 * brief: 去除高位零肢；len 变为 0 时符号规范化为 ZERO
 */
static void div_normalize(bigint_bin_ty *val)
{
    while ((val->len > 0U) && (val->limbs[val->len - 1U] == 0U)) {
        val->len--;
    }
    if (val->len == 0U) {
        val->sign = BIGINT_SIGN_ZERO_E;
    }
}

/*
 * brief: 以幅值赋值 val = mag（非负），mag 为 0 时规范化为零
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty set_u64_mag(bigint_bin_ty *val, uint64_t mag)
{
    if (mag == 0U) {
        val->len = 0U;
        val->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    const bigint_err_ty err = div_ensure_cap(val, 2U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    val->limbs[0] = (uint32_t)mag;
    const uint32_t high = (uint32_t)(mag >> 32U);
    if (high > 0U) {
        val->limbs[1] = high;
        val->len = 2U;
    } else {
        val->len = 1U;
    }
    val->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/*
 * brief: 求 uint32_t 的位长（最高有效位位置 + 1；0 为 0）
 */
static unsigned limb_bits(uint32_t x)
{
    unsigned bits = 0U;
    while (x != 0U) {
        bits++;
        x >>= 1U;
    }
    return bits;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：幅值除法                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 单肢幅值除法：quot = |lhs| / divisor，rem = |lhs| % divisor
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（对象不变）
 * note: 要求 divisor != 0 且 |lhs| >= divisor（由 mag_divmod 保证）
 */
static bigint_err_ty divmod_single(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, uint32_t divisor)
{
    bigint_err_ty err = div_ensure_cap(quot, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }

    uint64_t carry = 0U;
    size_t i = lhs->len;
    while (i > 0U) {
        i--;
        const uint64_t cur = (carry << 32U) | (uint64_t)lhs->limbs[i];
        quot->limbs[i] = (uint32_t)(cur / divisor);
        carry = cur % divisor;
    }
    quot->len = lhs->len;
    quot->sign = BIGINT_SIGN_POS_E;
    div_normalize(quot);
    return set_u64_mag(rem, carry);
}

/*
 * brief: 多肢幅值除法：quot = |lhs| / |rhs|，rem = |lhs| % |rhs|
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（对象不变）
 * note: 要求 |lhs| >= |rhs| 且 rhs 非零（由 bigint_bin_div_rem 保证）；
 *       实现为 Knuth《TAOCP》卷 2 §4.3.1 Algorithm D
 */
static bigint_err_ty mag_divmod(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const size_t n = rhs->len;
    const size_t m = lhs->len - n;

    if (n == 1U) {
        return divmod_single(quot, rem, lhs, rhs->limbs[0]);
    }
    if ((m == 0U) && (bigint_bin_cmp_abs(lhs, rhs) == 0)) {
        /* 齐长且相等：商 = 1，余 = 0 */
        bigint_err_ty err = bigint_bin_from_u64(quot, UINT64_C(1));
        if (err != BIGINT_OK_E) {
            return err;
        }
        return bigint_bin_from_u64(rem, 0U);
    }

    /* D1 规范化：取 s 使除数最高肢最高位为 1 */
    unsigned shift = 0U;
    {
        uint32_t top = rhs->limbs[n - 1U];
        while ((top & 0x80000000U) == 0U) {
            top <<= 1U;
            shift++;
        }
    }

    const size_t u_len = m + n + 1U;
    if ((u_len > SIZE_MAX / sizeof(uint32_t)) || (n > SIZE_MAX / sizeof(uint32_t))) {
        return BIGINT_ERR_OOM_E;
    }
    uint32_t *u = (uint32_t *)nex_malloc(u_len * sizeof(uint32_t));
    if (u == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    uint32_t *v = (uint32_t *)nex_malloc(n * sizeof(uint32_t));
    if (v == NULL) {
        free(u);
        return BIGINT_ERR_OOM_E;
    }

    /* u' = |lhs| << shift，长度 m + n + 1 */
    if (shift > 0U) {
        uint64_t carry = 0U;
        for (size_t i = 0U; i < lhs->len; i++) {
            const uint64_t cur = ((uint64_t)lhs->limbs[i] << shift) | carry;
            u[i] = (uint32_t)cur;
            carry = (uint64_t)lhs->limbs[i] >> (32U - shift);
        }
        u[lhs->len] = (uint32_t)carry;
    } else {
        memcpy(u, lhs->limbs, lhs->len * sizeof(uint32_t));
        u[lhs->len] = 0U;
    }

    /* v' = |rhs| << shift，长度 n */
    if (shift > 0U) {
        for (size_t i = 0U; i < n; i++) {
            const uint64_t cur = (uint64_t)rhs->limbs[i] << shift;
            const uint64_t prev = (i > 0U)
                    ? ((uint64_t)rhs->limbs[i - 1U] >> (32U - shift)) : 0U;
            v[i] = (uint32_t)(cur | prev);
        }
    } else {
        memcpy(v, rhs->limbs, n * sizeof(uint32_t));
    }

    const uint32_t vtop = v[n - 1U];
    const uint32_t vnext = v[n - 2U];

    bigint_err_ty err = div_ensure_cap(quot, m + 1U);
    if (err != BIGINT_OK_E) {
        free(u);
        free(v);
        return err;
    }
    err = div_ensure_cap(rem, n);
    if (err != BIGINT_OK_E) {
        free(u);
        free(v);
        return err;
    }

    /* D2 主循环：j = m .. 0 */
    size_t j = m + 1U;
    while (j > 0U) {
        j--;
        /* D3 试商 qhat 与余 rhat */
        const uint64_t num = ((uint64_t)u[j + n] << 32U) | u[j + n - 1U];
        uint64_t qhat = num / vtop;
        uint64_t rhat = num % vtop;
        while ((qhat >= NEX_BASE_U64)
                || (qhat * (uint64_t)vnext > ((rhat << 32U) | u[j + n - 2U]))) {
            qhat--;
            rhat += vtop;
            if (rhat >= NEX_BASE_U64) {
                break;
            }
        }
#ifdef NEX_DIV_DEBUG
        {
            fprintf(stderr, "[D] j=%zu num=%llu qhat0=%llu rhat0=%llu n=%zu m=%zu shift=%u vtop=%u vnext=%u\n",
                    j, (unsigned long long)num, (unsigned long long)(num / vtop),
                    (unsigned long long)(num % vtop), n, m, shift, vtop, vnext);
        }
#endif

        /* D4 乘减：u[j .. j+n] -= qhat × v。
         *
         * 采用 Hacker's Delight §9-2 divmnu 的 64 位有符号形式：
         *   t = u[j+i] - k - p_low          （int64，允许一路借到负数）
         *   k = p_high - (t >> 32)
         * 关键：t 必须是有符号且 t>>32 为算术右移。当
         * p_low + k > u[j+i] + 2^32（需借 2 次）时 t ∈ [-2^33, -2^32)，
         * 算术右移得 -2，k = p_high + 2，深层借位完整传播。
         * （TAOCP 条件借位形式只区分借 0/1，丢失此处借位并污染后续肢；
         *   见 D4 校验 trace：j=1 步 i=1 需借 2，原形式只记 1，最终余数
         *   误残留 2^69。）k 恒 ∈ [0, 2^32+2]，不溢出。 */
        int64_t k = 0;
        for (size_t i = 0U; i < n; i++) {
            const uint64_t p = qhat * (uint64_t)v[i];
            const int64_t t = (int64_t)(uint64_t)u[j + i] - k
                    - (int64_t)(uint32_t)p;
            u[j + i] = (uint32_t)t;
            k = (int64_t)(p >> 32U) - (t >> 32U);
        }

        /* D5 顶肢：t 有符号为负 ⇔ 整段结果小于 0，qhat 需减 1（D6） */
        const int64_t t = (int64_t)(uint64_t)u[j + n] - k;
        u[j + n] = (uint32_t)t;

        /* D6 修正：结果为负则商减一并加回除数 */
        if (t < 0) {
            qhat--;
            uint64_t carry = 0U;
            for (size_t i = 0U; i < n; i++) {
                const uint64_t sum = (uint64_t)u[j + i] + v[i] + carry;
                u[j + i] = (uint32_t)sum;
                carry = sum >> 32U;
            }
            const uint64_t sum = (uint64_t)u[j + n] + carry;
            u[j + n] = (uint32_t)sum;
        }

        /* 存商 */
        quot->limbs[j] = (uint32_t)qhat;
    }
    quot->len = m + 1U;
    quot->sign = BIGINT_SIGN_POS_E;
    div_normalize(quot);

    /* D8 反规范化：余数 = 被除数低 n 肢 >> shift */
    if (shift > 0U) {
        for (size_t i = 0U; i < n; i++) {
            const uint64_t lo = u[i];
            const uint64_t hi = (i + 1U < n) ? (uint64_t)u[i + 1U] : 0U;
            rem->limbs[i] = (uint32_t)((lo >> shift)
                    | (hi << (32U - shift)));
        }
    } else {
        memcpy(rem->limbs, u, n * sizeof(uint32_t));
    }
    rem->len = n;
    rem->sign = BIGINT_SIGN_POS_E;
    div_normalize(rem);

    free(u);
    free(v);
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API：带余除法 / 非负模余                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 带余除法，lhs = quot × rhs + rem（截断除法，与 C99 整数除法语义相同）
 * return: 除数为零返回 BIGINT_ERR_DIV_ZERO_E（quot / rem 不被修改）；
 *         内存不足返回 BIGINT_ERR_OOM_E；成功返回 BIGINT_OK_E
 */
bigint_err_ty bigint_bin_div_rem(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (rhs->len == 0U) {
        return BIGINT_ERR_DIV_ZERO_E;
    }

    bigint_bin_ty tmp_quot;
    bigint_bin_ty tmp_rem;
    bigint_err_ty err = bigint_bin_init(&tmp_quot);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin_init(&tmp_rem);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp_quot);
        return err;
    }

    /* |lhs| < |rhs|：商 0，余数拷贝被除数（带符号） */
    if (bigint_bin_cmp_abs(lhs, rhs) < 0) {
        err = bigint_bin_copy(&tmp_rem, lhs);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_from_u64(&tmp_quot, 0U);
        }
        if (err != BIGINT_OK_E) {
            bigint_bin_free(&tmp_quot);
            bigint_bin_free(&tmp_rem);
            return err;
        }
        if (quot != NULL) {
            bigint_bin_move(quot, &tmp_quot);
        } else {
            bigint_bin_free(&tmp_quot);
        }
        if (rem != NULL) {
            bigint_bin_move(rem, &tmp_rem);
        } else {
            bigint_bin_free(&tmp_rem);
        }
        return BIGINT_OK_E;
    }

    err = mag_divmod(&tmp_quot, &tmp_rem, lhs, rhs);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&tmp_quot);
        bigint_bin_free(&tmp_rem);
        return err;
    }

    /* 商幅值非零（|lhs| >= |rhs| > 0）；余数符号与被除数一致 */
    if (tmp_quot.len > 0U) {
        tmp_quot.sign = (lhs->sign == rhs->sign)
                ? BIGINT_SIGN_POS_E : BIGINT_SIGN_NEG_E;
    }
    if (tmp_rem.len > 0U) {
        tmp_rem.sign = lhs->sign;
    }

    if (quot != NULL) {
        bigint_bin_move(quot, &tmp_quot);
    } else {
        bigint_bin_free(&tmp_quot);
    }
    if (rem != NULL) {
        bigint_bin_move(rem, &tmp_rem);
    } else {
        bigint_bin_free(&tmp_rem);
    }
    return BIGINT_OK_E;
}

/*
 * brief: 计算非负余数 dst = val mod m（m > 0，结果 ∈ [0, m)）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: dst 为已初始化对象，被覆盖
 */
static bigint_err_ty abs_mod(bigint_bin_ty *dst, const bigint_bin_ty *val,
        const bigint_bin_ty *m)
{
    bigint_err_ty err = bigint_bin_div_rem(NULL, dst, val, m);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (dst->sign == BIGINT_SIGN_NEG_E) {
        /* 截断除法余数与 lhs 同号，dst ∈ (-m, 0)；非负化：dst += m
         * （r ∈ (-m, 0) ⇒ r + m ∈ (0, m)；不可用 m - r，
         *   例 -5 mod 7 = 2，m - r = 12 ≠ 2） */
        bigint_bin_ty sum;
        err = bigint_bin_init(&sum);
        if (err != BIGINT_OK_E) {
            return err;
        }
        err = bigint_bin_add(&sum, m, dst);   /* r + m ≡ r (mod m) */
        if (err != BIGINT_OK_E) {
            bigint_bin_free(&sum);
            return err;
        }
        bigint_bin_move(dst, &sum);
    }
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API：幂与模幂                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 幂 dst = base^exp，0^0 = 1
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_pow(bigint_bin_ty *dst, const bigint_bin_ty *base,
        uint64_t exp)
{
    if ((dst == NULL) || (base == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }

    const bool odd_exp = (exp & 1U) != 0U;
    bigint_bin_ty result;
    bigint_bin_ty factor;
    bigint_bin_ty tmp;
    bool result_ok = false;
    bool factor_ok = false;
    bool tmp_ok = false;
    bigint_err_ty err = bigint_bin_init(&result);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    result_ok = true;
    err = bigint_bin_init(&factor);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    factor_ok = true;
    err = bigint_bin_init(&tmp);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    tmp_ok = true;

    err = bigint_bin_from_u64(&result, UINT64_C(1));
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    err = bigint_bin_copy(&factor, base);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    /* 幂运算在幅值上进行，符号最后统一设置（避免中间量符号污染） */
    err = bigint_bin_abs(&factor);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }

    /* square-and-multiply，从最低有效位起 */
    uint64_t e = exp;
    while (e > 0U) {
        if ((e & 1U) != 0U) {
            err = bigint_bin_mul(&tmp, &result, &factor);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
            bigint_bin_move(&result, &tmp);
        }
        e >>= 1U;
        if (e > 0U) {
            err = bigint_bin_mul(&tmp, &factor, &factor);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
            bigint_bin_move(&factor, &tmp);
        }
    }

    /* 符号：负底数且指数为奇 → 负；结果为零则保持零 */
    if (odd_exp && (base->sign == BIGINT_SIGN_NEG_E) && (result.len > 0U)) {
        err = bigint_bin_neg(&result);
        if (err != BIGINT_OK_E) {
            goto cleanup;
        }
    }
    bigint_bin_move(dst, &result);
    err = BIGINT_OK_E;

cleanup:
    if (result_ok) {
        bigint_bin_free(&result);
    }
    if (factor_ok) {
        bigint_bin_free(&factor);
    }
    if (tmp_ok) {
        bigint_bin_free(&tmp);
    }
    return err;
}

/*
 * brief: 模幂 dst = base^exp mod mod，结果 ∈ [0, mod)
 * return: mod 为零返回 BIGINT_ERR_DIV_ZERO_E；exp 为负或 mod 为负返回
 *         BIGINT_ERR_INVALID_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 */
bigint_err_ty bigint_bin_pow_mod(bigint_bin_ty *dst, const bigint_bin_ty *base,
        const bigint_bin_ty *exp, const bigint_bin_ty *mod)
{
    if ((dst == NULL) || (base == NULL) || (exp == NULL) || (mod == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (mod->len == 0U) {
        return BIGINT_ERR_DIV_ZERO_E;
    }
    if (mod->sign == BIGINT_SIGN_NEG_E) {
        return BIGINT_ERR_INVALID_E;
    }
    if (exp->sign == BIGINT_SIGN_NEG_E) {
        return BIGINT_ERR_INVALID_E;
    }

    bigint_bin_ty result;
    bigint_bin_ty factor;
    bigint_bin_ty tmp;
    bool result_ok = false;
    bool factor_ok = false;
    bool tmp_ok = false;
    bigint_err_ty err = bigint_bin_init(&result);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    result_ok = true;
    err = bigint_bin_init(&factor);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    factor_ok = true;
    err = bigint_bin_init(&tmp);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    tmp_ok = true;

    /* result = 1 mod mod（mod 为 1 时自然得 0） */
    {
        bigint_bin_ty one;
        err = bigint_bin_init(&one);
        if (err != BIGINT_OK_E) {
            goto cleanup;
        }
        err = bigint_bin_from_u64(&one, UINT64_C(1));
        if (err == BIGINT_OK_E) {
            err = abs_mod(&result, &one, mod);
        }
        bigint_bin_free(&one);
        if (err != BIGINT_OK_E) {
            goto cleanup;
        }
    }
    /* factor = base mod mod（非负） */
    err = abs_mod(&factor, base, mod);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }

    /* 逐位平方-乘（从最低有效位起） */
    if (exp->len > 0U) {
        const size_t last = exp->len - 1U;
        const unsigned last_bits = limb_bits(exp->limbs[last]);
        for (size_t i = 0U; i < exp->len; i++) {
            const uint32_t word = exp->limbs[i];
            const unsigned bits = (i == last) ? last_bits : 32U;
            for (unsigned k = 0U; k < bits; k++) {
                if (((word >> k) & 1U) != 0U) {
                    err = bigint_bin_mul(&tmp, &result, &factor);
                    if (err != BIGINT_OK_E) {
                        goto cleanup;
                    }
                    err = abs_mod(&result, &tmp, mod);
                    if (err != BIGINT_OK_E) {
                        goto cleanup;
                    }
                }
                if (!((i == last) && (k + 1U == bits))) {
                    err = bigint_bin_mul(&tmp, &factor, &factor);
                    if (err != BIGINT_OK_E) {
                        goto cleanup;
                    }
                    err = abs_mod(&factor, &tmp, mod);
                    if (err != BIGINT_OK_E) {
                        goto cleanup;
                    }
                }
            }
        }
    }

    bigint_bin_move(dst, &result);
    err = BIGINT_OK_E;

cleanup:
    if (result_ok) {
        bigint_bin_free(&result);
    }
    if (factor_ok) {
        bigint_bin_free(&factor);
    }
    if (tmp_ok) {
        bigint_bin_free(&tmp);
    }
    return err;
}