/*
 * nex_bigint_dec_div.c：bigint_dec_ty 除法与幂实现。
 *
 * 职责（设计文档 §5.2、§5.3）：带余除法（Knuth《TAOCP》卷 2 Algorithm D，
 * 规范化 + 试商修正）、幂（square-and-multiply）、模幂（模平方-乘）。
 *
 * 除法语义：截断除法，与 C99 整数除法一致——商向零取整，余数符号与被除数
 * 相同（lhs = quot × rhs + rem，|rem| < |rhs|）。
 *
 * 基 10^9 的 uint64_t 累加安全性（§5.3）：
 *   - 试商两肢组合：u[j+n] × 10^9 + u[j+n−1] ≤ 10^18 − 1 < 2^60；
 *   - 试商修正项：qhat < 2·10^9、vnext < 10^9，qhat × vnext < 2·10^18 < 2^62；
 *   - 乘减段：qhat × v[i] ≤ (10^9−1)^2 < 2^60，借位不动点 k ≤ 10^9 − 1。
 * 规范化因子取 d = 10^9 / (vtop + 1)，使除数最高肢 ≥ 10^9 / 2（Knuth D1 的
 * 乘法形式；基 10^9 无最高位可左移，故用乘法代替 bin 的位移规范化）。
 *
 * 规范化不变式、别名约定与"失败时输出不变"契约同头文件。本文件仅处理
 * 幅值内部的中间量，符号由顶层函数统一设置；乘 / 减等复用公开 API。
 */

#include "nex/bigint/dec/nex_bigint_dec.h"

#include <stdlib.h>
#include <string.h>

/* 十进制肢基：每肢恰好 9 位十进制数字 */
#define NEX_DEC_BASE UINT32_C(1000000000)

/* 基的 uint64_t 形式，用于试商比较 */
#define NEX_DEC_BASE_U64 UINT64_C(1000000000)

/* ------------------------------------------------------------------ */
/* 内部辅助：容量与规范化（算法文件自包含，不依赖 dec.c 的 static 函数）      */
/* ------------------------------------------------------------------ */

/*
 * brief: 确保 val 的容量至少为 needed 肢，不足时扩至 max(2 * cap, needed)
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
static bigint_err_ty div_ensure_cap(bigint_dec_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }

    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
        return BIGINT_ERR_OOM_E;
    }

    uint32_t *new_limbs = (uint32_t *)realloc(val->limbs,
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
static void div_normalize(bigint_dec_ty *val)
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
 * note: uint64_t 最大约 1.8×10^19 < 10^27，至多 3 肢
 */
static bigint_err_ty set_u64_mag(bigint_dec_ty *val, uint64_t mag)
{
    if (mag == 0U) {
        val->len = 0U;
        val->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }

    const bigint_err_ty err = div_ensure_cap(val, 3U);
    if (err != BIGINT_OK_E) {
        return err;
    }

    size_t len = 0U;
    while (mag > 0U) {
        val->limbs[len] = (uint32_t)(mag % NEX_DEC_BASE_U64);
        mag /= NEX_DEC_BASE_U64;
        len++;
    }
    val->len = len;
    val->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/*
 * brief: 幅值就地除以单肢除数：limbs[0..len) /= divisor，返回余数
 * note: 两肢组合 rem × 10^9 + limbs[idx] ≤ 10^18 − 1 < 2^60（§5.3）；
 *       商可能含高位零肢，由调用方修剪
 */
static uint32_t div_small(uint32_t *limbs, size_t len, uint32_t divisor)
{
    uint64_t rem = 0U;
    for (size_t idx = len; idx-- > 0U;) {
        const uint64_t cur = rem * NEX_DEC_BASE_U64 + limbs[idx];
        limbs[idx] = (uint32_t)(cur / (uint64_t)divisor);
        rem = cur % (uint64_t)divisor;
    }
    return (uint32_t)rem;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：幅值除法                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: 单肢幅值除法：quot = |lhs| / divisor，rem = |lhs| % divisor
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（对象不变）
 * note: 要求 divisor != 0 且 |lhs| >= divisor（由 mag_divmod 保证）
 */
static bigint_err_ty divmod_single(bigint_dec_ty *quot, bigint_dec_ty *rem,
        const bigint_dec_ty *lhs, uint32_t divisor)
{
    bigint_err_ty err = div_ensure_cap(quot, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }

    /* 拷贝幅值后整体单肢除，商就地落在 quot */
    memcpy(quot->limbs, lhs->limbs, lhs->len * sizeof(uint32_t));
    quot->len = lhs->len;
    const uint32_t rem_limb = div_small(quot->limbs, quot->len, divisor);
    quot->sign = BIGINT_SIGN_POS_E;
    div_normalize(quot);
    return set_u64_mag(rem, rem_limb);
}

/*
 * brief: 规范化乘减主循环的一步：u[j .. j+n] -= qhat × v（就地）
 * param: u    被除数工作数组（长度 m + n + 1）
 * param: v    规范化除数（长度 n）
 * param: j    当前步偏移
 * param: n    除数肢数
 * param: qhat 试商（< 10^9）
 * return: 段结果为负（需 D6 加回除数）返回 true
 * note: 借位经 borrow 显式传播：p_low + borrow > u[j+i] 时借 1，
 *       k = p_high + borrow ≤ (10^9−2) + 1 < 10^9，无溢出（§5.3）
 */
static bool mul_sub_step(uint32_t *u, const uint32_t *v, size_t j, size_t n,
        uint64_t qhat)
{
    uint64_t k = 0U;  // 上一步的进位与借位合计
    for (size_t i = 0U; i < n; i++) {
        const uint64_t prod = qhat * (uint64_t)v[i];
        const uint64_t sub = (prod % NEX_DEC_BASE_U64) + k;
        const uint64_t cur = u[j + i];
        if (cur >= sub % NEX_DEC_BASE_U64) {
            u[j + i] = (uint32_t)(cur - sub % NEX_DEC_BASE_U64);
            k = prod / NEX_DEC_BASE_U64 + sub / NEX_DEC_BASE_U64;
        } else {
            u[j + i] = (uint32_t)(NEX_DEC_BASE_U64 + cur
                    - sub % NEX_DEC_BASE_U64);
            k = prod / NEX_DEC_BASE_U64 + sub / NEX_DEC_BASE_U64 + 1U;
        }
    }
    /* 顶肢：u[j+n] 减去 k；不够减则整段为负 */
    const uint64_t top = u[j + n];
    if (top >= k) {
        u[j + n] = (uint32_t)(top - k);
        return false;
    }
    u[j + n] = (uint32_t)(NEX_DEC_BASE_U64 + top - k);
    return true;
}

/*
 * brief: D6 加回除数：u[j .. j+n] += v（就地）；顶肢进位与先前借位相消
 */
static void add_back(uint32_t *u, const uint32_t *v, size_t j, size_t n)
{
    uint64_t carry = 0U;
    for (size_t i = 0U; i < n; i++) {
        const uint64_t sum = (uint64_t)u[j + i] + v[i] + carry;
        if (sum >= NEX_DEC_BASE_U64) {
            u[j + i] = (uint32_t)(sum - NEX_DEC_BASE_U64);
            carry = 1U;
        } else {
            u[j + i] = (uint32_t)sum;
            carry = 0U;
        }
    }
    // 顶肢 stored + carry 数学上等于 0（mod 10^9），取回范围内值
    uint64_t sum = (uint64_t)u[j + n] + carry;
    if (sum >= NEX_DEC_BASE_U64) {
        sum -= NEX_DEC_BASE_U64;
    }
    u[j + n] = (uint32_t)sum;
}

/*
 * brief: 多肢幅值除法：quot = |lhs| / |rhs|，rem = |lhs| % |rhs|
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（对象不变）
 * note: 要求 |lhs| >= |rhs| 且 rhs 非零（由 bigint_dec_div_rem 保证）；
 *       实现为 Knuth《TAOCP》卷 2 §4.3.1 Algorithm D（乘法规范化形式）
 */
static bigint_err_ty mag_divmod(bigint_dec_ty *quot, bigint_dec_ty *rem,
        const bigint_dec_ty *lhs, const bigint_dec_ty *rhs)
{
    const size_t n = rhs->len;
    const size_t m = lhs->len - n;

    if (n == 1U) {
        return divmod_single(quot, rem, lhs, rhs->limbs[0]);
    }
    if ((m == 0U) && (bigint_dec_cmp_abs(lhs, rhs) == 0)) {
        /* 齐长且相等：商 = 1，余 = 0 */
        bigint_err_ty err = bigint_dec_from_u64(quot, UINT64_C(1));
        if (err != BIGINT_OK_E) {
            return err;
        }
        return bigint_dec_from_u64(rem, 0U);
    }

    /* D1 规范化：d = 10^9 / (vtop + 1)，使除数最高肢 ≥ 10^9 / 2 */
    const uint32_t scale = (uint32_t)(NEX_DEC_BASE_U64
            / ((uint64_t)rhs->limbs[n - 1U] + 1U));

    const size_t u_len = m + n + 1U;
    if ((u_len > SIZE_MAX / sizeof(uint32_t)) || (n > SIZE_MAX / sizeof(uint32_t))) {
        return BIGINT_ERR_OOM_E;
    }
    uint32_t *u = (uint32_t *)malloc(u_len * sizeof(uint32_t));
    if (u == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    uint32_t *v = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (v == NULL) {
        free(u);
        return BIGINT_ERR_OOM_E;
    }

    /* u' = |lhs| × d，长度 m + n + 1（d == 1 时为原值加 0 顶肢） */
    {
        uint64_t carry = 0U;
        for (size_t i = 0U; i < lhs->len; i++) {
            const uint64_t cur = (uint64_t)lhs->limbs[i] * scale + carry;
            u[i] = (uint32_t)(cur % NEX_DEC_BASE_U64);
            carry = cur / NEX_DEC_BASE_U64;
        }
        u[lhs->len] = (uint32_t)carry;
    }
    /* v' = |rhs| × d，长度 n（d·v < 10^(9n)，不产生溢出肢，见文件头论证） */
    {
        uint64_t carry = 0U;
        for (size_t i = 0U; i < n; i++) {
            const uint64_t cur = (uint64_t)rhs->limbs[i] * scale + carry;
            v[i] = (uint32_t)(cur % NEX_DEC_BASE_U64);
            carry = cur / NEX_DEC_BASE_U64;
        }
        // carry 恒为 0：d × vtop + 进位 < 10^9（d ≤ 10^9/(vtop+1)）
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
        /* D3 试商 qhat 与余 rhat（两肢组合 ≤ 10^18 − 1 < 2^60） */
        const uint64_t num = (uint64_t)u[j + n] * NEX_DEC_BASE_U64
                + u[j + n - 1U];
        uint64_t qhat = num / vtop;
        uint64_t rhat = num % vtop;
        while ((qhat >= NEX_DEC_BASE_U64)
                || (qhat * (uint64_t)vnext
                        > rhat * NEX_DEC_BASE_U64 + u[j + n - 2U])) {
            qhat--;
            rhat += vtop;
            if (rhat >= NEX_DEC_BASE_U64) {
                break;
            }
        }

        /* D4 / D5 乘减；D6 结果为负则商减一并加回除数 */
        if (mul_sub_step(u, v, j, n, qhat)) {
            qhat--;
            add_back(u, v, j, n);
        }

        /* 存商 */
        quot->limbs[j] = (uint32_t)qhat;
    }
    quot->len = m + 1U;
    quot->sign = BIGINT_SIGN_POS_E;
    div_normalize(quot);

    /* D8 反规范化：余数 = 被除数低 n 肢 / d（单肢除法） */
    memcpy(rem->limbs, u, n * sizeof(uint32_t));
    rem->len = n;
    if (scale > 1U) {
        (void)div_small(rem->limbs, rem->len, scale);
    }
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
bigint_err_ty bigint_dec_div_rem(bigint_dec_ty *quot, bigint_dec_ty *rem,
        const bigint_dec_ty *lhs, const bigint_dec_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (rhs->len == 0U) {
        return BIGINT_ERR_DIV_ZERO_E;
    }

    bigint_dec_ty tmp_quot;
    bigint_dec_ty tmp_rem;
    bigint_err_ty err = bigint_dec_init(&tmp_quot);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_dec_init(&tmp_rem);
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&tmp_quot);
        return err;
    }

    /* |lhs| < |rhs|：商 0，余数拷贝被除数（带符号） */
    if (bigint_dec_cmp_abs(lhs, rhs) < 0) {
        err = bigint_dec_copy(&tmp_rem, lhs);
        if (err == BIGINT_OK_E) {
            err = bigint_dec_from_u64(&tmp_quot, 0U);
        }
        if (err != BIGINT_OK_E) {
            bigint_dec_free(&tmp_quot);
            bigint_dec_free(&tmp_rem);
            return err;
        }
        if (quot != NULL) {
            bigint_dec_move(quot, &tmp_quot);
        } else {
            bigint_dec_free(&tmp_quot);
        }
        if (rem != NULL) {
            bigint_dec_move(rem, &tmp_rem);
        } else {
            bigint_dec_free(&tmp_rem);
        }
        return BIGINT_OK_E;
    }

    err = mag_divmod(&tmp_quot, &tmp_rem, lhs, rhs);
    if (err != BIGINT_OK_E) {
        bigint_dec_free(&tmp_quot);
        bigint_dec_free(&tmp_rem);
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
        bigint_dec_move(quot, &tmp_quot);
    } else {
        bigint_dec_free(&tmp_quot);
    }
    if (rem != NULL) {
        bigint_dec_move(rem, &tmp_rem);
    } else {
        bigint_dec_free(&tmp_rem);
    }
    return BIGINT_OK_E;
}

/*
 * brief: 计算非负余数 dst = val mod m（m > 0，结果 ∈ [0, m)）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: dst 为已初始化对象，被覆盖
 */
static bigint_err_ty abs_mod(bigint_dec_ty *dst, const bigint_dec_ty *val,
        const bigint_dec_ty *m)
{
    bigint_err_ty err = bigint_dec_div_rem(NULL, dst, val, m);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (dst->sign == BIGINT_SIGN_NEG_E) {
        /* 截断除法余数与 lhs 同号，dst ∈ (-m, 0)；非负化：dst += m
         * （r ∈ (-m, 0) ⇒ r + m ∈ (0, m)；不可用 m - r，
         *   例 -5 mod 7 = 2，m - r = 12 ≠ 2） */
        bigint_dec_ty sum;
        err = bigint_dec_init(&sum);
        if (err != BIGINT_OK_E) {
            return err;
        }
        err = bigint_dec_add(&sum, m, dst);   /* r + m ≡ r (mod m) */
        if (err != BIGINT_OK_E) {
            bigint_dec_free(&sum);
            return err;
        }
        bigint_dec_move(dst, &sum);
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
bigint_err_ty bigint_dec_pow(bigint_dec_ty *dst, const bigint_dec_ty *base,
        uint64_t exp)
{
    if ((dst == NULL) || (base == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }

    const bool odd_exp = (exp & 1U) != 0U;
    bigint_dec_ty result;
    bigint_dec_ty factor;
    bigint_dec_ty tmp;
    bool result_ok = false;
    bool factor_ok = false;
    bool tmp_ok = false;
    bigint_err_ty err = bigint_dec_init(&result);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    result_ok = true;
    err = bigint_dec_init(&factor);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    factor_ok = true;
    err = bigint_dec_init(&tmp);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    tmp_ok = true;

    err = bigint_dec_from_u64(&result, UINT64_C(1));
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    err = bigint_dec_copy(&factor, base);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    /* 幂运算在幅值上进行，符号最后统一设置（避免中间量符号污染） */
    err = bigint_dec_abs(&factor);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }

    /* square-and-multiply，从最低有效位起 */
    uint64_t e = exp;
    while (e > 0U) {
        if ((e & 1U) != 0U) {
            err = bigint_dec_mul(&tmp, &result, &factor);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
            bigint_dec_move(&result, &tmp);
        }
        e >>= 1U;
        if (e > 0U) {
            err = bigint_dec_mul(&tmp, &factor, &factor);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
            bigint_dec_move(&factor, &tmp);
        }
    }

    /* 符号：负底数且指数为奇 → 负；结果为零则保持零 */
    if (odd_exp && (base->sign == BIGINT_SIGN_NEG_E) && (result.len > 0U)) {
        err = bigint_dec_neg(&result);
        if (err != BIGINT_OK_E) {
            goto cleanup;
        }
    }
    bigint_dec_move(dst, &result);
    err = BIGINT_OK_E;

cleanup:
    if (result_ok) {
        bigint_dec_free(&result);
    }
    if (factor_ok) {
        bigint_dec_free(&factor);
    }
    if (tmp_ok) {
        bigint_dec_free(&tmp);
    }
    return err;
}

/*
 * brief: 模幂 dst = base^exp mod mod，结果 ∈ [0, mod)
 * return: mod 为零返回 BIGINT_ERR_DIV_ZERO_E；exp 为负或 mod 为负返回
 *         BIGINT_ERR_INVALID_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 十进制肢不便逐位扫描，指数经工作副本反复折半取奇偶位
 *       （与逐位扫描等价，仍从最低有效位起平方-乘）
 */
bigint_err_ty bigint_dec_pow_mod(bigint_dec_ty *dst, const bigint_dec_ty *base,
        const bigint_dec_ty *exp, const bigint_dec_ty *mod)
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

    bigint_dec_ty result;
    bigint_dec_ty factor;
    bigint_dec_ty tmp;
    bigint_dec_ty e_work;
    bool result_ok = false;
    bool factor_ok = false;
    bool tmp_ok = false;
    bool e_work_ok = false;
    bigint_err_ty err = bigint_dec_init(&result);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    result_ok = true;
    err = bigint_dec_init(&factor);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    factor_ok = true;
    err = bigint_dec_init(&tmp);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    tmp_ok = true;
    err = bigint_dec_init(&e_work);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    e_work_ok = true;

    /* result = 1 mod mod（mod 为 1 时自然得 0） */
    {
        bigint_dec_ty one;
        err = bigint_dec_init(&one);
        if (err != BIGINT_OK_E) {
            goto cleanup;
        }
        err = bigint_dec_from_u64(&one, UINT64_C(1));
        if (err == BIGINT_OK_E) {
            err = abs_mod(&result, &one, mod);
        }
        bigint_dec_free(&one);
        if (err != BIGINT_OK_E) {
            goto cleanup;
        }
    }
    /* factor = base mod mod（非负） */
    err = abs_mod(&factor, base, mod);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }
    /* 指数工作副本（取 |exp|；exp 非负已校验） */
    err = bigint_dec_copy(&e_work, exp);
    if (err != BIGINT_OK_E) {
        goto cleanup;
    }

    /* 折半取位，自最低有效位起平方-乘 */
    while (e_work.len > 0U) {
        const bool bit = (e_work.limbs[0] & 1U) != 0U;
        (void)div_small(e_work.limbs, e_work.len, 2U);
        div_normalize(&e_work);
        if (bit) {
            err = bigint_dec_mul(&tmp, &result, &factor);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
            err = abs_mod(&result, &tmp, mod);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
        }
        if (e_work.len > 0U) {
            err = bigint_dec_mul(&tmp, &factor, &factor);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
            err = abs_mod(&factor, &tmp, mod);
            if (err != BIGINT_OK_E) {
                goto cleanup;
            }
        }
    }

    bigint_dec_move(dst, &result);
    err = BIGINT_OK_E;

cleanup:
    if (result_ok) {
        bigint_dec_free(&result);
    }
    if (factor_ok) {
        bigint_dec_free(&factor);
    }
    if (tmp_ok) {
        bigint_dec_free(&tmp);
    }
    if (e_work_ok) {
        bigint_dec_free(&e_work);
    }
    return err;
}
