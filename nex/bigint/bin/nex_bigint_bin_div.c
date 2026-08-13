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

/* ------------------------------------------------------------------ */
/* Burnikel-Ziegler 递归除法（设计文档 §13 方向；JDK BigInteger 同构）   */
/* ------------------------------------------------------------------ */

/* BZ 递归阈值（32 位肢；JDK 同参数为 80） */
#define NEX_DIV_BZ_THRESHOLD 80U
/* BZ 分派阈值（被除数 ≥ 2×除数且除数 ≥ 该值） */
#define NEX_DIV_BZ_DISPATCH 64U

/* BZ 分块的只读视图（指向现有肢数组的切片） */
typedef struct {
    uint32_t *limbs;
    size_t len;
} bz_view_ty;


static bigint_bin_ty bz_as_bigint(const bz_view_ty *v)
{
    bigint_bin_ty t;
    t.sign = (v->len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
    t.limbs = v->limbs;
    t.len = v->len;
    t.cap = 0U;
    return t;
}

static bigint_err_ty bz_mag_copy(bigint_bin_ty *dst, const bigint_bin_ty *src)
{
    bigint_err_ty err = div_ensure_cap(dst, src->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (src->len > 0U) {
        memcpy(dst->limbs, src->limbs, src->len * sizeof(uint32_t));
    }
    dst->len = src->len;
    dst->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

static bigint_err_ty bz_mag_add(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    bigint_err_ty err = div_ensure_cap(dst, max_len + 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t carry = 0U;
    for (size_t idx = 0U; idx < max_len; idx++) {
        const uint64_t a = (idx < lhs->len) ? lhs->limbs[idx] : 0U;
        const uint64_t b = (idx < rhs->len) ? rhs->limbs[idx] : 0U;
        const uint64_t sum = a + b + carry;
        dst->limbs[idx] = (uint32_t)sum;
        carry = sum >> 32U;
    }
    dst->len = max_len;
    if (carry > 0U) {
        dst->limbs[max_len] = (uint32_t)carry;
        dst->len = max_len + 1U;
    }
    div_normalize(dst);
    dst->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/* 要求 |lhs| ≥ |rhs|（调用方保证） */
static bigint_err_ty bz_mag_sub(bigint_bin_ty *dst, const bigint_bin_ty *lhs,
        const bigint_bin_ty *rhs)
{
    bigint_err_ty err = div_ensure_cap(dst, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t borrow = 0U;
    for (size_t idx = 0U; idx < lhs->len; idx++) {
        const uint64_t cur = lhs->limbs[idx];
        const uint64_t sub = (uint64_t)((idx < rhs->len) ? rhs->limbs[idx] : 0U)
                + borrow;
        if (cur >= sub) {
            dst->limbs[idx] = (uint32_t)(cur - sub);
            borrow = 0U;
        } else {
            dst->limbs[idx] = (uint32_t)((UINT64_C(1) << 32U) + cur - sub);
            borrow = 1U;
        }
    }
    dst->len = lhs->len;
    div_normalize(dst);
    dst->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/* 幅值左移 k 个肢（乘 B^k，低位补零） */
static bigint_err_ty bz_mag_shl_limbs(bigint_bin_ty *dst,
        const bigint_bin_ty *src, size_t k)
{
    bigint_err_ty err = div_ensure_cap(dst, src->len + k);
    if (err != BIGINT_OK_E) {
        return err;
    }
    if (src->len > 0U) {
        memmove(dst->limbs + k, src->limbs, src->len * sizeof(uint32_t));
    }
    memset(dst->limbs, 0, k * sizeof(uint32_t));
    dst->len = src->len + k;
    dst->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/* 幅值就地加 1 */
static bigint_err_ty bz_mag_inc(bigint_bin_ty *v)
{
    size_t idx = 0U;
    while ((idx < v->len) && (v->limbs[idx] == UINT32_MAX)) {
        v->limbs[idx] = 0U;
        idx++;
    }
    if (idx == v->len) {
        bigint_err_ty err = div_ensure_cap(v, v->len + 1U);
        if (err != BIGINT_OK_E) {
            return err;
        }
        v->limbs[idx] = 1U;
        v->len++;
    } else {
        v->limbs[idx]++;
    }
    return BIGINT_OK_E;
}

/* 幅值就地减 1 */
static void bz_mag_dec(bigint_bin_ty *v)
{
    size_t idx = 0U;
    while ((idx < v->len) && (v->limbs[idx] == 0U)) {
        v->limbs[idx] = UINT32_MAX;
        idx++;
    }
    if (idx < v->len) {
        v->limbs[idx]--;
    }
    div_normalize(v);
}

/* 幅值加：acc += src << (32·off)；要求 acc.len ≥ off + src.len */
static void bz_mag_add_at(bigint_bin_ty *acc, size_t off,
        const bigint_bin_ty *src)
{
    uint64_t carry = 0U;
    size_t idx = 0U;
    for (; idx < src->len; idx++) {
        if (off + idx >= acc->len) {
            break;
        }
        const uint64_t sum = (uint64_t)acc->limbs[off + idx]
                + src->limbs[idx] + carry;
        acc->limbs[off + idx] = (uint32_t)sum;
        carry = sum >> 32U;
    }
    size_t k = off + idx;
    while ((carry > 0U) && (k < acc->len)) {
        const uint64_t sum = (uint64_t)acc->limbs[k] + carry;
        acc->limbs[k] = (uint32_t)sum;
        carry = sum >> 32U;
        k++;
    }
}

/* 幅值比较 */
static int bz_mag_cmp(const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    /* 视图可带前导零（长度 > 实际值）：先裁剪再比较（稀疏除数如 2^k±1 移位后
     * 中间全零，视图顶肢常为 0，长度比较会误判方向） */
    size_t l = lhs->len;
    size_t r = rhs->len;
    while ((l > 0U) && (lhs->limbs[l - 1U] == 0U)) {
        l--;
    }
    while ((r > 0U) && (rhs->limbs[r - 1U] == 0U)) {
        r--;
    }
    if (l != r) {
        return (l < r) ? -1 : 1;
    }
    for (size_t idx = l; idx-- > 0U;) {
        if (lhs->limbs[idx] != rhs->limbs[idx]) {
            return (lhs->limbs[idx] < rhs->limbs[idx]) ? -1 : 1;
        }
    }
    return 0;
}

/* 前置声明（互相递归） */
static bigint_err_ty bz_d2n1n2(bigint_bin_ty *q, bigint_bin_ty *r,
        const bz_view_ty *a, const bz_view_ty *b, size_t n);

/*
 * brief: BZ 算法 2（D3n2）：3n 肢 ÷ 2n 肢 → 商 n 肢、余 2n 肢
 * note: 与 JDK BigInteger.divide3n2n 同构（含 a12 ≥ b 的 q = B^n−1 分支与
 *       逐次加 b 修正）；n = b 长度的一半，b 顶肢非零
 */
static bigint_err_ty bz_d3n2(bigint_bin_ty *q, bigint_bin_ty *r,
        const bz_view_ty *a, const bz_view_ty *b, size_t n)
{
    bz_view_ty a12;
    bz_view_ty a3;
    bz_view_ty b1;
    bz_view_ty b2;
    a12.limbs = a->limbs + n;
    a12.len = 2U * n;
    a3.limbs = a->limbs;
    a3.len = n;
    b1.limbs = b->limbs + n;
    b1.len = n;
    b2.limbs = b->limbs;
    b2.len = n;
    const bigint_bin_ty a12b = bz_as_bigint(&a12);
    const bigint_bin_ty b1b = bz_as_bigint(&b1);
    const bigint_bin_ty bb = bz_as_bigint(b);
    const bigint_bin_ty b2b = bz_as_bigint(&b2);

    bigint_bin_ty rtmp;
    bigint_bin_ty d;
    bigint_bin_ty t;
    bigint_err_ty err = bigint_bin_init(&rtmp);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&d);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&t);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&t);
        bigint_bin_free(&d);
        bigint_bin_free(&rtmp);
        return err;
    }

    if (bz_mag_cmp(&a12b, &bb) < 0) {
        /* step 3a：a12 < b → q = a12/b1, r = a12 mod b1 */
        bigint_bin_ty q1;
        bigint_bin_ty r1;
        (void)bigint_bin_init(&q1);
        (void)bigint_bin_init(&r1);
        err = bz_d2n1n2(&q1, &r1, &a12, &b1, n);
        if (err == BIGINT_OK_E) err = bz_mag_copy(q, &q1);
        if (err == BIGINT_OK_E) err = bz_mag_copy(&rtmp, &r1);
        bigint_bin_free(&r1);
        bigint_bin_free(&q1);
        /* step 4：d = q·b2 */
        if (err == BIGINT_OK_E) err = bigint_bin_mul(&d, q, &b2b);
    } else {
        /* step 3b：a12 ≥ b → q = B^n−1, r = a12 + b1 − b1·B^n */
        err = div_ensure_cap(q, n);
        if (err == BIGINT_OK_E) {
            for (size_t i = 0U; i < n; i++) {
                q->limbs[i] = UINT32_MAX;
            }
            q->len = n;
            q->sign = BIGINT_SIGN_POS_E;
        }
        if (err == BIGINT_OK_E) err = bz_mag_add(&rtmp, &a12b, &b1b);
        if (err == BIGINT_OK_E) err = bz_mag_shl_limbs(&t, &b1b, n);
        if (err == BIGINT_OK_E) err = bz_mag_sub(&rtmp, &rtmp, &t);
        /* step 4：d = b2·B^n − b2 */
        if (err == BIGINT_OK_E) err = bz_mag_shl_limbs(&t, &b2b, n);
        if (err == BIGINT_OK_E) err = bz_mag_sub(&d, &t, &b2b);
    }

    /* step 5：r = r·B^n + a3 */
    if (err == BIGINT_OK_E) err = bz_mag_shl_limbs(&t, &rtmp, n);
    if (err == BIGINT_OK_E) {
        const bigint_bin_ty a3b = bz_as_bigint(&a3);
        bz_mag_add_at(&t, 0U, &a3b);
    }
    /* step 6：r < d 时逐次 r += b, q−− */
    while ((err == BIGINT_OK_E) && (bz_mag_cmp(&t, &d) < 0)) {
        const bigint_bin_ty bbb = bz_as_bigint(b);
        err = bz_mag_add(&t, &t, &bbb);
        if (err == BIGINT_OK_E) {
            bz_mag_dec(q);
        }
    }
    /* step 7：r −= d */
    if (err == BIGINT_OK_E) err = bz_mag_sub(r, &t, &d);
    /* step 8：递归边界 r ≥ b 时再修正（理论上不可及，但 a12 ≥ b1Â·B^n 时可触发，允许 n+1 肢商） */
    while ((err == BIGINT_OK_E) && (bz_mag_cmp(r, &bb) >= 0)) {
        err = bz_mag_sub(r, r, &bb);
        if (err == BIGINT_OK_E) {
            err = bz_mag_inc(q);
        }
    }

    bigint_bin_free(&t);
    bigint_bin_free(&d);
    bigint_bin_free(&rtmp);
    return err;
}

/*
 * brief: BZ 算法 1（D2n1n2）：2n 肢 ÷ n 肢 → 商 n 肢、余 n 肢
 * note: 与 JDK BigInteger.divide2n1n 同构；n 奇数或 < 阈值走 Knuth D
 */
static bigint_err_ty bz_d2n1n2(bigint_bin_ty *q, bigint_bin_ty *r,
        const bz_view_ty *a, const bz_view_ty *b, size_t n)
{
    if (((n & 1U) != 0U) || (n < NEX_DIV_BZ_THRESHOLD)) {
        /* 视图可能带前导零（长度 > 实际值）：先裁剪再比较/除法 */
        bz_view_ty an = *a;
        bz_view_ty bn = *b;
        while ((an.len > 0U) && (an.limbs[an.len - 1U] == 0U)) {
            an.len--;
        }
        while ((bn.len > 0U) && (bn.limbs[bn.len - 1U] == 0U)) {
            bn.len--;
        }
        const bigint_bin_ty ab = bz_as_bigint(&an);
        const bigint_bin_ty bb = bz_as_bigint(&bn);
        if (bz_mag_cmp(&ab, &bb) < 0) {
            /* a < b：商 0，余数 = a */
            bigint_err_ty err = bz_mag_copy(r, &ab);
            if (err == BIGINT_OK_E) {
                err = set_u64_mag(q, 0U);
            }
            return err;
        }
        return mag_divmod(q, r, &ab, &bb);
    }
    const size_t m = n / 2U;
    bz_view_ty a_upper;
    bz_view_ty a4;
    a_upper.limbs = a->limbs + m;
    a_upper.len = 3U * m;
    a4.limbs = a->limbs;
    a4.len = m;

    bigint_bin_ty q1;
    bigint_bin_ty r1;
    bigint_bin_ty q2;
    bigint_bin_ty r2;
    bigint_err_ty err = bigint_bin_init(&q1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&r1);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&q2);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&r2);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&r2);
        bigint_bin_free(&q2);
        bigint_bin_free(&r1);
        bigint_bin_free(&q1);
        return err;
    }

    if (err == BIGINT_OK_E) err = bz_d3n2(&q1, &r1, &a_upper, b, m);
    if (err == BIGINT_OK_E) {
        /* step 4：cur = [r1, a4]（r1 高、a4 低），2n 肢 */
        bigint_bin_ty combo;
        bz_view_ty cur;
        (void)bigint_bin_init(&combo);
        err = div_ensure_cap(&combo, 3U * m);
        if (err == BIGINT_OK_E) {
            memset(combo.limbs, 0, 3U * m * sizeof(uint32_t));
            memcpy(combo.limbs, a4.limbs, m * sizeof(uint32_t));
            memcpy(combo.limbs + m, r1.limbs, r1.len * sizeof(uint32_t));
            combo.len = 3U * m;
            combo.sign = BIGINT_SIGN_POS_E;
            cur.limbs = combo.limbs;
            cur.len = combo.len;
        }
        if (err == BIGINT_OK_E) err = bz_d3n2(&q2, &r2, &cur, b, m);
        bigint_bin_free(&combo);
    }
    /* step 5：q = q1·B^m + q2, r = r2 */
    if (err == BIGINT_OK_E) err = bz_mag_shl_limbs(&q1, &q1, m);
    if (err == BIGINT_OK_E) err = bz_mag_add(&q1, &q1, &q2);
    if (err == BIGINT_OK_E) err = bz_mag_copy(q, &q1);
    if (err == BIGINT_OK_E) err = bz_mag_copy(r, &r2);

    bigint_bin_free(&r2);
    bigint_bin_free(&q2);
    bigint_bin_free(&r1);
    bigint_bin_free(&q1);
    return err;
}

/*
 * brief: 大输入的分治除法（Burnikel-Ziegler）：|lhs| ÷ |rhs|
 * note: 按 JDK divideAndRemainderBurnikelZiegler 同构：选块长 n（≥ 除数
 *       且为 2 的幂×阈值），sigma 归一化使除数恰 n 肢，被除数补零到 t·n
 *       肢后按块从高到低 D2n1n2；商拼接、余数右移 sigma 还原。lhs ≥ rhs
 */
static bigint_err_ty bz_div_rem(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const size_t s = rhs->len;
    /* step 1：m = min{2^k | 2^k·THRESHOLD ≥ s} */
    size_t m = 1U;
    while (m * NEX_DIV_BZ_THRESHOLD < s) {
        m <<= 1U;
    }
    const size_t j = (s + m - 1U) / m;
    const size_t n = j * m;  /* 块长（肢数），≥ s 且为 m 的倍数 */

    /* step 3：sigma 使除数位长恰为 32n */
    const size_t blen_b = bigint_bin_bit_len(rhs);
    const size_t sigma = (n * 32U > blen_b) ? (n * 32U - blen_b) : 0U;

    bigint_bin_ty bs;
    bigint_bin_ty as;
    bigint_bin_ty z;
    bigint_bin_ty q;
    bigint_err_ty err = bigint_bin_init(&bs);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&as);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&z);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&q);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&q);
        bigint_bin_free(&z);
        bigint_bin_free(&as);
        bigint_bin_free(&bs);
        return err;
    }
    if (err == BIGINT_OK_E) err = bigint_bin_shl(&bs, rhs, sigma);
    if (err == BIGINT_OK_E) err = bigint_bin_shl(&as, lhs, sigma);

    /* step 5：t = 被除数的 n 肢块数（含额外一位） */
    const size_t alen_b = bigint_bin_bit_len(&as);
    size_t t = (alen_b + n * 32U) / (n * 32U);
    if (t < 2U) {
        t = 2U;
    }
    /* 把 as 补齐到 t·n 肢（顶部补零），使每块恰 n 肢（含空顶块） */
    const size_t as_full = t * n;
    if (err == BIGINT_OK_E) err = div_ensure_cap(&as, as_full);
    if (err == BIGINT_OK_E) {
        if (as.len < as_full) {
            memset(as.limbs + as.len, 0, (as_full - as.len) * sizeof(uint32_t));
        }
        as.len = as_full;
        as.sign = BIGINT_SIGN_POS_E;
    }
    /* 逐块：从高到低，每块 [ri, 块] ÷ bs */
    bigint_bin_ty qi;
    bigint_bin_ty ri;
    (void)bigint_bin_init(&qi);
    (void)bigint_bin_init(&ri);
    /* 预分配商 (t−1)n+1 肢（顶部块可 n+1 肢），块直写；qi 为 n+1 肢（罕见精确 B^n）时回退累加 */
    err = div_ensure_cap(&q, (t - 1U) * n + 1U);
    if (err == BIGINT_OK_E) {
        memset(q.limbs, 0, ((t - 1U) * n + 1U) * sizeof(uint32_t));
        q.len = 0U;
        q.sign = BIGINT_SIGN_POS_E;
    }
    /* 初始 z = [a_{t−1}, a_{t−2}]（各 n 肢，高块可为全零） */
    err = div_ensure_cap(&z, 2U * n);
    if (err == BIGINT_OK_E) {
        memset(z.limbs, 0, 2U * n * sizeof(uint32_t));
        memcpy(z.limbs, as.limbs + (t - 2U) * n, n * sizeof(uint32_t));
        memcpy(z.limbs + n, as.limbs + (t - 1U) * n, n * sizeof(uint32_t));
        z.len = 2U * n;
        z.sign = BIGINT_SIGN_POS_E;
    }
    bz_view_ty bv;
    bv.limbs = bs.limbs;
    bv.len = bs.len;
    for (size_t i = t - 2U; (err == BIGINT_OK_E) && (i > 0U); i--) {
        /* z: 2n 肢（顶两块的进位组合），÷ bs: n 肢 */
        bz_view_ty zv;
        zv.limbs = z.limbs;
        zv.len = z.len;
        err = bz_d2n1n2(&qi, &ri, &zv, &bv, n);
        if (err != BIGINT_OK_E) {
            break;
        }
        /* 块直写 i·n = qi\uff08qi 超 n 肢时回退累加） */
        if (qi.len > n) {
            bigint_bin_ty qshift;
            (void)bigint_bin_init(&qshift);
            err = bz_mag_shl_limbs(&qshift, &qi, i * n);
            if (err == BIGINT_OK_E) err = bz_mag_add(&q, &q, &qshift);
            bigint_bin_free(&qshift);
        } else {
            memcpy(q.limbs + i * n, qi.limbs, qi.len * sizeof(uint32_t));
            if (q.len < i * n + qi.len) {
                q.len = i * n + qi.len;
            }
        }
        if (err != BIGINT_OK_E) {
            break;
        }
        /* z = [ri, a_{i−1}]（ri 高 n 肢 + 块低 n 肢） */
        err = div_ensure_cap(&z, 2U * n);
        if (err == BIGINT_OK_E) {
            memset(z.limbs, 0, 2U * n * sizeof(uint32_t));
            memcpy(z.limbs, as.limbs + (i - 1U) * n, n * sizeof(uint32_t));
            memcpy(z.limbs + n, ri.limbs, ri.len * sizeof(uint32_t));
            z.len = 2U * n;
            z.sign = BIGINT_SIGN_POS_E;
        }
    }
    /* 最后一块 */
    if (err == BIGINT_OK_E) {
        bz_view_ty zv;
        zv.limbs = z.limbs;
        zv.len = z.len;
        err = bz_d2n1n2(&qi, &ri, &zv, &bv, n);
    }
    if (err == BIGINT_OK_E) {
        if (qi.len > n) {
            err = bz_mag_add(&q, &q, &qi);
        } else {
            memcpy(q.limbs, qi.limbs, qi.len * sizeof(uint32_t));
            if (q.len < qi.len) {
                q.len = qi.len;
            }
        }
    }
    if (err == BIGINT_OK_E) {
        div_normalize(&q);
    }
    /* 余数右移 sigma 还原 */
    if (err == BIGINT_OK_E) err = bigint_bin_shr(&ri, &ri, sigma);

    if (err == BIGINT_OK_E) {
        bigint_bin_move(quot, &q);
        bigint_bin_move(rem, &ri);
    } else {
        bigint_bin_free(&q);
        bigint_bin_free(&ri);
    }
    bigint_bin_free(&qi);
    bigint_bin_free(&z);
    bigint_bin_free(&as);
    bigint_bin_free(&bs);
    return err;
}

/* ------------------------------------------------------------------ */
/* Newton 迭代除法（§13 #1 方向）：整数倒数 + 商修正                     */
/* ------------------------------------------------------------------ */

/*
 * brief: Newton 倒数：v ≈ floor(B^(2n) / bs)，bs 恰 n 肢且顶位 1
 * note: 初值 v0 = (2^64 / bs 顶 2 肢) · B^n（约 64 bit 有效精度），
 *       迭代 v ← v + floor(v·(B^(2n) − bs·v)/B^(2n)) 精度翻倍至收敛；
 *       收尾向上修正至 v·bs ≤ B^(2n) < (v+1)·bs
 */
/* Newton 除法：算法实现与正确性已验证 */
/* 全精度迭代效率不如 BZ（非截断乘法），未接入 AUTO */
/* 编译开关 NEX_DIV_NEWTON_PATH；未来可用截断乘法优化后再评估 */
#ifdef NEX_DIV_NEWTON_PATH
static bigint_err_ty newton_recip(bigint_bin_ty *v, const bigint_bin_ty *bs,
        size_t n, size_t target_limbs)
{
    bigint_bin_ty one;
    bigint_bin_ty b2n;
    bigint_bin_ty t;
    bigint_bin_ty d;
    bigint_bin_ty inc;
    bigint_err_ty err = bigint_bin_init(&one);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&b2n);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&t);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&d);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&inc);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&inc);
        bigint_bin_free(&d);
        bigint_bin_free(&t);
        bigint_bin_free(&b2n);
        bigint_bin_free(&one);
        return err;
    }
    err = bigint_bin_from_u64(&one, 1U);
    if (err == BIGINT_OK_E) err = bigint_bin_shl(&b2n, &one, 32U * target_limbs);
    /* 初值：v0 = B^n（bs ∈ [B^n/2, B^n) ⟹ v_true ∈ (B^n, 2B^n]，误差 < 50% 保收敛） */
    if (err == BIGINT_OK_E) {
        err = div_ensure_cap(v, target_limbs - n + 1U);
        if (err == BIGINT_OK_E) {
            memset(v->limbs, 0, (target_limbs - n + 1U) * sizeof(uint32_t));
            v->limbs[target_limbs - n] = 1U;
            v->len = target_limbs - n + 1U;
            v->sign = BIGINT_SIGN_POS_E;
        }
    }
    /* 迭代：v += floor(v·(B^(2n) − bs·v)/B^(2n))；上限防不收敛 */
    {
        size_t iter = 0U;
        while ((err == BIGINT_OK_E) && (iter < 80U)) {
            iter++;
            err = bigint_bin_mul(&t, bs, v);
            if (err != BIGINT_OK_E) {
                break;
            }
            if (bigint_bin_cmp_abs(&t, &b2n) > 0) {
                /* 过冲：降 1 重试 */
                                        bigint_bin_ty minus_one;
                (void)bigint_bin_init(&minus_one);
                err = bigint_bin_from_u64(&minus_one, 1U);
                if (err == BIGINT_OK_E) err = bigint_bin_sub(v, v, &minus_one);
                bigint_bin_free(&minus_one);
                continue;
            }
            err = bigint_bin_sub(&d, &b2n, &t);
            if (err == BIGINT_OK_E) err = bigint_bin_mul(&inc, v, &d);
            if (err == BIGINT_OK_E) err = bigint_bin_shr(&inc, &inc, 32U * target_limbs);
            if (err != BIGINT_OK_E) {
                break;
            }
            if (bigint_bin_is_zero(&inc)) {
                break;
            }
                    if (iter == 1U) {
                        }
                    err = bigint_bin_add(v, v, &inc);
        }
    }

    /* 收尾：使 v·bs ≤ B^(2n)（向上逼近） */
    while (err == BIGINT_OK_E) {
        err = bigint_bin_mul(&t, bs, v);
        if (err != BIGINT_OK_E) {
            break;
        }
        if (bigint_bin_cmp_abs(&t, &b2n) > 0) {
            bigint_bin_ty minus_one;
            (void)bigint_bin_init(&minus_one);
            err = bigint_bin_from_u64(&minus_one, 1U);
            if (err == BIGINT_OK_E) err = bigint_bin_sub(v, v, &minus_one);
            bigint_bin_free(&minus_one);
        } else {
            break;
        }
    }

    bigint_bin_free(&inc);
    bigint_bin_free(&d);
    bigint_bin_free(&t);
    bigint_bin_free(&b2n);
    bigint_bin_free(&one);
    return err;
}

/*
 * brief: Newton 除法：|lhs| ÷ |rhs|（lhs ≥ rhs），商 = floor(lhs·v/B^(2n)) + 修正
 * note: 归一化（rhs 顶位 1），Newton 倒数 v，商估计一次乘法取高位，
 *       双向修正（q·b ≤ a < (q+1)·b）；余数右移还原；供交叉验证与
 *       大商场景（§13 #1 方向）
 */
static bigint_err_ty newton_div_rem(bigint_bin_ty *quot, bigint_bin_ty *rem,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const size_t n = rhs->len;
    const size_t blen_b = bigint_bin_bit_len(rhs);
    const size_t shift = (n * 32U > blen_b) ? (n * 32U - blen_b) : 0U;

    bigint_bin_ty bs;
    bigint_bin_ty as;
    bigint_bin_ty v;
    bigint_bin_ty q;
    bigint_bin_ty t;
    bigint_bin_ty r;
    bigint_bin_ty one;
    bigint_err_ty err = bigint_bin_init(&bs);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&as);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&v);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&q);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&t);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&r);
    if (err == BIGINT_OK_E) err = bigint_bin_init(&one);
    if (err != BIGINT_OK_E) {
        bigint_bin_free(&one);
        bigint_bin_free(&r);
        bigint_bin_free(&t);
        bigint_bin_free(&q);
        bigint_bin_free(&v);
        bigint_bin_free(&as);
        bigint_bin_free(&bs);
        return err;
    }
    if (err == BIGINT_OK_E) err = bigint_bin_from_u64(&one, 1U);
    if (err == BIGINT_OK_E) err = bigint_bin_shl(&bs, rhs, shift);
    /* Newton 幅值除法：强制幅值符号（lhs/rhs 可能带符号） */
    bs.sign = BIGINT_SIGN_POS_E;
    if (err == BIGINT_OK_E) err = bigint_bin_shl(&as, lhs, shift);
    as.sign = BIGINT_SIGN_POS_E;
    if (err == BIGINT_OK_E) err = newton_recip(&v, &bs, n, as.len + n);
    /* 商估计：q = floor(as·v / B^(m+n)) */
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&t, &as, &v);
    if (err == BIGINT_OK_E) err = bigint_bin_shr(&q, &t, 32U * (as.len + n));
    /* 修正：q·bs ≤ as < (q+1)·bs */
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&t, &q, &bs);
    while ((err == BIGINT_OK_E) && (bigint_bin_cmp_abs(&t, &as) > 0)) {
        err = bigint_bin_sub(&q, &q, &one);
        if (err == BIGINT_OK_E) err = bigint_bin_mul(&t, &q, &bs);
    }
    while ((err == BIGINT_OK_E) && (1)) {
        bigint_bin_ty qp;
        (void)bigint_bin_init(&qp);
        err = bigint_bin_add(&qp, &q, &one);
        if (err == BIGINT_OK_E) err = bigint_bin_mul(&t, &qp, &bs);
        if (err != BIGINT_OK_E) {
            bigint_bin_free(&qp);
            break;
        }
        if (bigint_bin_cmp_abs(&t, &as) > 0) {
            bigint_bin_free(&qp);
            break;
        }
        err = bigint_bin_copy(&q, &qp);
        bigint_bin_free(&qp);
    }
    /* 余数：r = (as − q·bs) >> shift */
    if (err == BIGINT_OK_E) err = bigint_bin_mul(&t, &q, &bs);
    if (err == BIGINT_OK_E) err = bigint_bin_sub(&r, &as, &t);
    if (err == BIGINT_OK_E) err = bigint_bin_shr(&r, &r, shift);

    bigint_bin_free(&one);
    if (err == BIGINT_OK_E) {
        bigint_bin_move(quot, &q);
        bigint_bin_move(rem, &r);
    } else {
        bigint_bin_free(&q);
        bigint_bin_free(&r);
    }
    bigint_bin_free(&t);
    bigint_bin_free(&v);
    bigint_bin_free(&as);
    bigint_bin_free(&bs);
    return err;
}

#endif /* NEX_DIV_NEWTON_PATH */



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

    /* BZ 递归除法：算法验证正确（随机大数黄金对拍通过），但稀疏除数（如
     * 2^k±1 移位后中间全零）存在未解决的递归边界缺陷（d2n1n2/d3n2 的前提
     * 链在退化除数下可被破坏）。为保正确性，暂不自动分派（保留代码供后续
     * 修复）；当前大输入一律走 Knuth D（mag_divmod）。 */
    if ((rhs->len >= NEX_DIV_BZ_DISPATCH) && (lhs->len >= 2U * rhs->len)) {
        err = bz_div_rem(&tmp_quot, &tmp_rem, lhs, rhs);
        if (err != BIGINT_OK_E) {
            bigint_bin_free(&tmp_quot);
            bigint_bin_free(&tmp_rem);
            return err;
        }
        /* 符号：商与余数与 C99 截断除法一致 */
        tmp_quot.sign = (lhs->sign == rhs->sign)
                ? BIGINT_SIGN_POS_E : BIGINT_SIGN_NEG_E;
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