/*
 * nex_bigint_bin64.c：64 位肢二进制大整数（设计文档 §13 #3）。
 *
 * 职责：生命周期、基本转换、加减乘（schoolbook + Karatsuba，阈值 32）。
 * 规范化不变式与错误码语义与 bigint_bin 一致（§3.4、§4.3）。
 *
 * 正确性要点（随实现维护）：
 *   - 64 位进位 / 借位经回绕检测显式计算（sum < a / cur < sub），禁止
 *     依赖编译器溢出行为；sub 的 (rhs+borrow) 回绕同样以 sub < rhs 检测；
 *   - schoolbook 内层累加 li·wide[j] + dst[i+j] + carry 的 128 位进位
 *     链：p.hi + carry1 + carry2，数学上 ≤ 2^64−1（真值 ≤ 2^128−1），
 *     uint64 不回绕；
 *   - 128 位乘法经 nex_u128_mul：GCC/Clang 走 __int128（-Wpedantic 下
 *     局部抑制诊断），MSVC 走便携 32 位半字 4 乘（见头文件 §13 #3）。
 */

#include "nex/bigint/bin64/nex_bigint_bin64.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

/* Karatsuba 切换阈值（64 位肢数） */
#define NEX_BIN64_MUL_CUTOFF 32U

/* ------------------------------------------------------------------ */
/* 内部辅助：容量与规范化                                                */
/* ------------------------------------------------------------------ */

static bigint_err_ty b64_ensure(bigint_bin64_ty *val, size_t needed)
{
    if (needed <= val->cap) {
        return BIGINT_OK_E;
    }
    const size_t doubled = val->cap * 2U;
    const size_t new_cap = (doubled > needed) ? doubled : needed;
    if (new_cap > SIZE_MAX / sizeof(uint64_t)) {
        return BIGINT_ERR_OOM_E;
    }
    uint64_t *new_limbs = (uint64_t *)nex_realloc(val->limbs,
            new_cap * sizeof(uint64_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = new_cap;
    return BIGINT_OK_E;
}

static void b64_normalize(bigint_bin64_ty *val)
{
    while ((val->len > 0U) && (val->limbs[val->len - 1U] == 0U)) {
        val->len--;
    }
    if (val->len == 0U) {
        val->sign = BIGINT_SIGN_ZERO_E;
    }
}

/* ------------------------------------------------------------------ */
/* 128 位乘法：编译器探测 + 便携回退（设计文档 §13 #3）                  */
/* ------------------------------------------------------------------ */

#if defined(NEX_BIN64_FORCE_PORTABLE) || !defined(__SIZEOF_INT128__)
/*
 * brief: 便携 64×64→128（32 位半字 4 乘），MSVC 与强制回退时使用
 */
static nex_u128_ty u128_mul_portable(uint64_t a, uint64_t b)
{
    const uint32_t a0 = (uint32_t)a;
    const uint32_t a1 = (uint32_t)(a >> 32U);
    const uint32_t b0 = (uint32_t)b;
    const uint32_t b1 = (uint32_t)(b >> 32U);
    const uint64_t p00 = (uint64_t)a0 * b0;
    const uint64_t p01 = (uint64_t)a0 * b1;
    const uint64_t p10 = (uint64_t)a1 * b0;
    const uint64_t p11 = (uint64_t)a1 * b1;
    const uint64_t mid = (p00 >> 32U) + (uint32_t)p01 + (uint32_t)p10;
    nex_u128_ty r;
    r.lo = (p00 & 0xFFFFFFFFU) | (mid << 32U);
    r.hi = p11 + (p01 >> 32U) + (p10 >> 32U) + (mid >> 32U);
    return r;
}

nex_u128_ty nex_u128_mul(uint64_t a, uint64_t b)
{
    return u128_mul_portable(a, b);
}
#else
/*
 * 硬件路径：GCC / Clang 的 128 位扩展。ISO C99 不识别 __int128，
 * -Wpedantic 下局部抑制该诊断（仅此分支；MSVC 无 __SIZEOF_INT128__，
 * 走上方便携路径；测试可经 NEX_BIN64_FORCE_PORTABLE 强制便携）
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
nex_u128_ty nex_u128_mul(uint64_t a, uint64_t b)
{
    const unsigned __int128 t = (unsigned __int128)a * b;
    nex_u128_ty r;
    r.lo = (uint64_t)t;
    r.hi = (uint64_t)(t >> 64U);
    return r;
}
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------ */
/* 内部辅助：幅值加减（64 位进位 / 借位经回绕检测）                      */
/* ------------------------------------------------------------------ */

static bigint_err_ty b64_mag_add(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    const bigint_err_ty err = b64_ensure(dst, max_len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t carry = 0U;
    for (size_t idx = 0U; idx < max_len; idx++) {
        const uint64_t a = (idx < lhs->len) ? lhs->limbs[idx] : 0U;
        const uint64_t b = (idx < rhs->len) ? rhs->limbs[idx] : 0U;
        const uint64_t sum = a + b;
        const uint64_t acc = sum + carry;
        dst->limbs[idx] = acc;
        carry = ((sum < a) || (acc < sum)) ? 1U : 0U;
    }
    dst->len = max_len;
    if (carry > 0U) {
        dst->limbs[max_len] = carry;
        dst->len = max_len + 1U;
    }
    b64_normalize(dst);
    return BIGINT_OK_E;
}

/* 要求 |lhs| ≥ |rhs|（调用方保证） */
static bigint_err_ty b64_mag_sub(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    const bigint_err_ty err = b64_ensure(dst, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t borrow = 0U;
    for (size_t idx = 0U; idx < lhs->len; idx++) {
        const uint64_t cur = lhs->limbs[idx];
        const uint64_t rhs_l = (idx < rhs->len) ? rhs->limbs[idx] : 0U;
        const uint64_t sub = rhs_l + borrow;
        dst->limbs[idx] = cur - sub;
        borrow = ((sub < rhs_l) || (cur < sub)) ? 1U : 0U;
    }
    dst->len = lhs->len;
    b64_normalize(dst);
    return BIGINT_OK_E;
}

static int b64_cmp_abs(const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    if (lhs->len != rhs->len) {
        return (lhs->len < rhs->len) ? -1 : 1;
    }
    for (size_t idx = lhs->len; idx-- > 0U;) {
        if (lhs->limbs[idx] != rhs->limbs[idx]) {
            return (lhs->limbs[idx] < rhs->limbs[idx]) ? -1 : 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 对外 API：生命周期                                                    */
/* ------------------------------------------------------------------ */

bigint_err_ty bigint_bin64_init(bigint_bin64_ty *val)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    val->sign = BIGINT_SIGN_ZERO_E;
    val->limbs = NULL;
    val->len = 0U;
    val->cap = 0U;
    return BIGINT_OK_E;
}

bigint_err_ty bigint_bin64_init_cap(bigint_bin64_ty *val, size_t cap)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    val->sign = BIGINT_SIGN_ZERO_E;
    val->limbs = NULL;
    val->len = 0U;
    val->cap = 0U;
    if (cap > 0U) {
        val->limbs = (uint64_t *)nex_malloc(cap * sizeof(uint64_t));
        if (val->limbs == NULL) {
            return BIGINT_ERR_OOM_E;
        }
        val->cap = cap;
    }
    return BIGINT_OK_E;
}

void bigint_bin64_free(bigint_bin64_ty *val)
{
    if (val == NULL) {
        return;
    }
    free(val->limbs);
    val->limbs = NULL;
    val->len = 0U;
    val->cap = 0U;
    val->sign = BIGINT_SIGN_ZERO_E;
}

bigint_err_ty bigint_bin64_copy(bigint_bin64_ty *dst,
        const bigint_bin64_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (dst == src) {
        return BIGINT_OK_E;
    }
    bigint_err_ty err = b64_ensure(dst, src->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (src->len > 0U) {
        memcpy(dst->limbs, src->limbs, src->len * sizeof(uint64_t));
    }
    dst->len = src->len;
    dst->sign = src->sign;
    return BIGINT_OK_E;
}

void bigint_bin64_move(bigint_bin64_ty *dst, bigint_bin64_ty *src)
{
    if ((dst == NULL) || (src == NULL) || (dst == src)) {
        return;
    }
    free(dst->limbs);
    dst->limbs = src->limbs;
    dst->len = src->len;
    dst->cap = src->cap;
    dst->sign = src->sign;
    src->limbs = NULL;
    src->len = 0U;
    src->cap = 0U;
    src->sign = BIGINT_SIGN_ZERO_E;
}

bigint_err_ty bigint_bin64_shrink(bigint_bin64_ty *val)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len == val->cap) {
        return BIGINT_OK_E;
    }
    if (val->len == 0U) {
        free(val->limbs);
        val->limbs = NULL;
        val->cap = 0U;
        return BIGINT_OK_E;
    }
    uint64_t *new_limbs = (uint64_t *)nex_realloc(val->limbs,
            val->len * sizeof(uint64_t));
    if (new_limbs == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    val->limbs = new_limbs;
    val->cap = val->len;
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API：基本转换与断言                                              */
/* ------------------------------------------------------------------ */

bigint_err_ty bigint_bin64_from_u64(bigint_bin64_ty *val, uint64_t value)
{
    if (val == NULL) {
        return BIGINT_ERR_INVALID_E;
    }
    bigint_err_ty err = b64_ensure(val, 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    val->limbs[0] = value;
    val->len = (value == 0U) ? 0U : 1U;
    val->sign = (value == 0U) ? BIGINT_SIGN_ZERO_E : BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

bigint_err_ty bigint_bin64_to_u64(const bigint_bin64_ty *val, uint64_t *out)
{
    if ((val == NULL) || (out == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (val->len == 0U) {
        *out = 0U;
        return BIGINT_OK_E;
    }
    if (val->len > 1U) {
        return BIGINT_ERR_OVERFLOW_E;
    }
    *out = val->limbs[0];
    return BIGINT_OK_E;
}

int bigint_bin64_cmp(const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    if (lhs->sign != rhs->sign) {
        return (lhs->sign < rhs->sign) ? -1 : 1;
    }
    if (lhs->sign == BIGINT_SIGN_ZERO_E) {
        return 0;
    }
    const int c = b64_cmp_abs(lhs, rhs);
    return (lhs->sign == BIGINT_SIGN_POS_E) ? c : -c;
}

bool bigint_bin64_is_zero(const bigint_bin64_ty *val)
{
    return val->sign == BIGINT_SIGN_ZERO_E;
}

/* ------------------------------------------------------------------ */
/* 对外 API：加减（符号处理同 bigint_bin 的 add_sub 模式）                */
/* ------------------------------------------------------------------ */

/*
 * brief: dst = lhs ± rhs（is_sub 为真时翻转 rhs 符号）
 * note: dst 允许与 lhs / rhs 别名（内部经 tmp 规避）
 */
static bigint_err_ty b64_add_sub(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs, bool is_sub)
{
    bigint_sign_ty rhs_sign = rhs->sign;
    if (is_sub && (rhs_sign != BIGINT_SIGN_ZERO_E)) {
        rhs_sign = (rhs_sign == BIGINT_SIGN_POS_E)
                ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }

    if (lhs->sign == BIGINT_SIGN_ZERO_E) {
        /* 0 ± rhs = ±rhs（拷贝幅值 + 符号） */
        bigint_bin64_ty tmp;
        (void)bigint_bin64_init(&tmp);
        bigint_err_ty err = bigint_bin64_copy(&tmp, rhs);
        if (err == BIGINT_OK_E) {
            tmp.sign = rhs_sign;
            bigint_bin64_move(dst, &tmp);
        } else {
            bigint_bin64_free(&tmp);
        }
        return err;
    }
    if (rhs_sign == BIGINT_SIGN_ZERO_E) {
        return bigint_bin64_copy(dst, lhs);
    }

    bigint_err_ty err = BIGINT_OK_E;
    if (lhs->sign == rhs_sign) {
        err = b64_mag_add(dst, lhs, rhs);
        if (err == BIGINT_OK_E) {
            dst->sign = lhs->sign;
        }
        return err;
    }

    const int c = b64_cmp_abs(lhs, rhs);
    if (c == 0) {
        dst->len = 0U;
        dst->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    const bigint_bin64_ty *big = (c > 0) ? lhs : rhs;
    const bigint_bin64_ty *small = (c > 0) ? rhs : lhs;
    err = b64_mag_sub(dst, big, small);
    if (err == BIGINT_OK_E) {
        dst->sign = (c > 0) ? lhs->sign : rhs_sign;
    }
    return err;
}

bigint_err_ty bigint_bin64_add(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((dst == lhs) || (dst == rhs)) {
        bigint_bin64_ty tmp;
        (void)bigint_bin64_init(&tmp);
        const bigint_err_ty err = b64_add_sub(&tmp, lhs, rhs, false);
        if (err == BIGINT_OK_E) {
            bigint_bin64_move(dst, &tmp);
        } else {
            bigint_bin64_free(&tmp);
        }
        return err;
    }
    return b64_add_sub(dst, lhs, rhs, false);
}

bigint_err_ty bigint_bin64_sub(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((dst == lhs) || (dst == rhs)) {
        bigint_bin64_ty tmp;
        (void)bigint_bin64_init(&tmp);
        const bigint_err_ty err = b64_add_sub(&tmp, lhs, rhs, true);
        if (err == BIGINT_OK_E) {
            bigint_bin64_move(dst, &tmp);
        } else {
            bigint_bin64_free(&tmp);
        }
        return err;
    }
    return b64_add_sub(dst, lhs, rhs, true);
}

/* ------------------------------------------------------------------ */
/* 对外 API：乘法（schoolbook + Karatsuba）                              */
/* ------------------------------------------------------------------ */

static bigint_err_ty b64_mul_schoolbook(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    const bigint_bin64_ty *narrow = (lhs->len <= rhs->len) ? lhs : rhs;
    const bigint_bin64_ty *wide = (narrow == lhs) ? rhs : lhs;
    const size_t n = narrow->len;
    const size_t m = wide->len;
    const size_t total = n + m;

    const bigint_err_ty err = b64_ensure(dst, total);
    if (err != BIGINT_OK_E) {
        return err;
    }
    memset(dst->limbs, 0, total * sizeof(uint64_t));

    for (size_t i = 0U; i < n; i++) {
        const uint64_t li = narrow->limbs[i];
        if (li == 0U) {
            continue;
        }
        uint64_t carry = 0U;
        for (size_t j = 0U; j < m; j++) {
            const nex_u128_ty p = nex_u128_mul(li, wide->limbs[j]);
            const uint64_t acc1 = p.lo + dst->limbs[i + j];
            const uint64_t c1 = (acc1 < p.lo) ? 1U : 0U;
            const uint64_t acc2 = acc1 + carry;
            const uint64_t c2 = (acc2 < acc1) ? 1U : 0U;
            dst->limbs[i + j] = acc2;
            /* 数学上 p.hi + c1 + c2 ≤ 2^64−1（真值 ≤ 2^128−1），无回绕 */
            carry = p.hi + c1 + c2;
        }
        size_t k = i + m;
        while ((carry > 0U) && (k < total)) {
            const uint64_t sum = dst->limbs[k] + carry;
            dst->limbs[k] = sum;
            carry = (sum < carry) ? 1U : 0U;
            k++;
        }
    }
    dst->len = total;
    b64_normalize(dst);
    return BIGINT_OK_E;
}

static void b64_mag_add_at(bigint_bin64_ty *acc, size_t off,
        const bigint_bin64_ty *src)
{
    if (src->len == 0U) {
        return;
    }
    uint64_t carry = 0U;
    size_t idx = 0U;
    for (; idx < src->len; idx++) {
        if (off + idx >= acc->len) {
            break;
        }
        const uint64_t sum = acc->limbs[off + idx] + src->limbs[idx];
        const uint64_t acc2 = sum + carry;
        acc->limbs[off + idx] = acc2;
        carry = ((sum < src->limbs[idx]) || (acc2 < sum)) ? 1U : 0U;
    }
    size_t k = off + idx;
    while ((carry > 0U) && (k < acc->len)) {
        const uint64_t sum = acc->limbs[k] + carry;
        acc->limbs[k] = sum;
        carry = (sum < carry) ? 1U : 0U;
        k++;
    }
}

static bigint_err_ty b64_mul_karatsuba(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs, size_t cutoff)
{
    if (lhs->len < rhs->len) {
        const bigint_bin64_ty *swap = lhs;
        lhs = rhs;
        rhs = swap;
    }
    if ((rhs->len <= cutoff) || (lhs->len == 0U) || (rhs->len == 0U)) {
        const bigint_err_ty err = b64_ensure(dst, lhs->len + rhs->len);
        if (err != BIGINT_OK_E) {
            return err;
        }
        return b64_mul_schoolbook(dst, lhs, rhs);
    }

    const size_t m = lhs->len / 2U;
    const size_t c_len = (rhs->len > m) ? rhs->len - m : 0U;

    bigint_bin64_ty a;  /* 高半（含最高肢，恒非零） */
    bigint_bin64_ty b;  /* 低 m 肢 */
    bigint_bin64_ty c;
    bigint_bin64_ty d;
    a.sign = BIGINT_SIGN_POS_E;
    a.limbs = lhs->limbs + m;
    a.len = lhs->len - m;
    a.cap = 0U;
    b.sign = BIGINT_SIGN_POS_E;
    b.limbs = lhs->limbs;
    b.len = m;
    b.cap = 0U;
    c.sign = (c_len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    c.limbs = (c_len > 0U) ? (rhs->limbs + m) : rhs->limbs;
    c.len = c_len;
    c.cap = 0U;
    d.sign = BIGINT_SIGN_POS_E;
    d.limbs = rhs->limbs;
    d.len = (rhs->len < m) ? rhs->len : m;
    d.cap = 0U;

    bigint_bin64_ty z0;
    bigint_bin64_ty z1;
    bigint_bin64_ty z2;
    bigint_bin64_ty s;
    bigint_bin64_ty t;
    bigint_bin64_ty acc;
    bigint_err_ty err = bigint_bin64_init(&z0);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin64_init(&z1);
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&z0);
        return err;
    }
    err = bigint_bin64_init(&z2);
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&z0);
        bigint_bin64_free(&z1);
        return err;
    }
    err = bigint_bin64_init(&s);
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&z0);
        bigint_bin64_free(&z1);
        bigint_bin64_free(&z2);
        return err;
    }
    err = bigint_bin64_init(&t);
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&z0);
        bigint_bin64_free(&z1);
        bigint_bin64_free(&z2);
        bigint_bin64_free(&s);
        return err;
    }
    err = bigint_bin64_init(&acc);
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&z0);
        bigint_bin64_free(&z1);
        bigint_bin64_free(&z2);
        bigint_bin64_free(&s);
        bigint_bin64_free(&t);
        return err;
    }

    bigint_err_ty rc = BIGINT_OK_E;
    if (rc == BIGINT_OK_E) {
        rc = b64_mul_karatsuba(&z2, &a, &c, cutoff);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_mul_karatsuba(&z0, &b, &d, cutoff);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_mag_add(&s, &a, &b);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_mag_add(&t, &c, &d);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_mul_karatsuba(&z1, &s, &t, cutoff);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_mag_sub(&z1, &z1, &z0);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_mag_sub(&z1, &z1, &z2);
    }
    if (rc == BIGINT_OK_E) {
        rc = b64_ensure(&acc, lhs->len + rhs->len);
    }
    if (rc == BIGINT_OK_E) {
        acc.len = lhs->len + rhs->len;
        memset(acc.limbs, 0, acc.len * sizeof(uint64_t));
        b64_mag_add_at(&acc, 0U, &z0);
        b64_mag_add_at(&acc, m, &z1);
        b64_mag_add_at(&acc, 2U * m, &z2);
        b64_normalize(&acc);
        acc.sign = BIGINT_SIGN_POS_E;
        bigint_bin64_move(dst, &acc);
    }

    bigint_bin64_free(&z0);
    bigint_bin64_free(&z1);
    bigint_bin64_free(&z2);
    bigint_bin64_free(&s);
    bigint_bin64_free(&t);
    if (rc != BIGINT_OK_E) {
        bigint_bin64_free(&acc);
    }
    return rc;
}

bigint_err_ty bigint_bin64_mul(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    const int64_t neg = ((lhs->sign == BIGINT_SIGN_NEG_E)
            != (rhs->sign == BIGINT_SIGN_NEG_E)) ? 1 : 0;

    bigint_bin64_ty tmp;
    (void)bigint_bin64_init(&tmp);
    bigint_err_ty err = b64_mul_karatsuba(&tmp, lhs, rhs,
            NEX_BIN64_MUL_CUTOFF);
    if (err == BIGINT_OK_E) {
        if (tmp.len > 0U) {
            tmp.sign = (neg != 0) ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
        }
        bigint_bin64_move(dst, &tmp);
    } else {
        bigint_bin64_free(&tmp);
    }
    return err;
}
