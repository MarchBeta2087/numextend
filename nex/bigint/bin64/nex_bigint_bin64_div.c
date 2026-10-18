/*
 * nex_bigint_bin64_div.c：64 位肢带余除法（Knuth《TAOCP》卷 2 Algorithm D）。
 *
 * 职责：bigint_bin64 的原生除法，基 2^64；不再经 32 位肢 bin 转换。
 *
 * 正确性要点（随实现维护）：
 *   - 单肢除法与 Algorithm D 的试商都需要 128/64 除法，且满足 hi < d
 *     （商恰为 64 位）。GCC/Clang 走 __int128；MSVC 等走以 2^32 为基的
 *     专用 Knuth D（4 字 ÷ 2 字，32×32→64 中间量；可经
 *     NEX_BIN64_FORCE_PORTABLE 强制）。
 *   - D3 试商：u[j+n] 至多等于 v[n-1]（规范化后 v[n-1] ≥ 2^63）。相等时
 *     qhat 取 2^64−1，rhat 若上溢则跳过修正；否则用 128 位乘积与
 *     (rhat, u[j+n-2]) 比较后递减。
 *   - D4 乘减：qhat·v[i] 的 128 位高半 p.hi ≤ 2^64−3，加上低半进位
 *     （至多 1）不会回绕；减法的借位至多 1，以 b1 | b2 合并。
 *   - D6 修正：结果为负则 qhat 减一，并把 v 加回；加回的进位至多 1。
 *   - 不允许依赖有符号溢出行为（与 bin64 其他算法一致）。
 *
 * 除法语义与 bigint_bin 相同：截断除法，商向零取整，余数符号与被除数
 * 一致（lhs = quot × rhs + rem，|rem| < |rhs|）；除数为零返回
 * BIGINT_ERR_DIV_ZERO_E；失败时输出参数不变。
 */

#include "nex/bigint/bin64/nex_bigint_bin64.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_alloc.h"

#include <stdlib.h>
#include <string.h>

/* 超大输入回退 32 位肢 bin 的 Burnikel-Ziegler（64 位肢数阈值）：
 * 实测 ~1024 肢起两者接近，2048 肢以上 BZ 的 O(n^1.585) 明显优于
 * 本文件的 O(n²) Knuth D。 */
#define NEX_BIN64_DIV_BZ_THRESHOLD 1024U

/* ------------------------------------------------------------------ */
/* 内部辅助：容量、规范化、幅值比较                                       */
/* ------------------------------------------------------------------ */

static bigint_err_ty d64_ensure(bigint_bin64_ty *val, size_t needed)
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

static void d64_normalize(bigint_bin64_ty *val)
{
    while ((val->len > 0U) && (val->limbs[val->len - 1U] == 0U)) {
        val->len--;
    }
    if (val->len == 0U) {
        val->sign = BIGINT_SIGN_ZERO_E;
    }
}

static bigint_err_ty d64_set_u64(bigint_bin64_ty *val, uint64_t mag)
{
    if (mag == 0U) {
        val->len = 0U;
        val->sign = BIGINT_SIGN_ZERO_E;
        return BIGINT_OK_E;
    }
    const bigint_err_ty err = d64_ensure(val, 1U);
    if (err != BIGINT_OK_E) {
        return err;
    }
    val->limbs[0] = mag;
    val->len = 1U;
    val->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

static int d64_cmp_abs(const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
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

/* 64 位前导零个数（0 返回 64） */
static unsigned d64_clz(uint64_t x)
{
    unsigned bits = 0U;
    if (x == 0U) {
        return 64U;
    }
    while ((x & (UINT64_C(1) << 63U)) == 0U) {
        x <<= 1U;
        bits++;
    }
    return bits;
}

/* ------------------------------------------------------------------ */
/* 128/64 除法（hi < d，商恰为 64 位）                                   */
/* ------------------------------------------------------------------ */

#if defined(NEX_BIN64_FORCE_PORTABLE) || !defined(__SIZEOF_INT128__)
/*
 * brief: 128/64 除法，除数为 32 位（便携路径的小除数分支）
 * note: 要求 hi < v 且 v < 2^32；以 2^32 为基做四字长除
 */
static uint64_t u128_divmod_u32(uint64_t hi, uint64_t lo, uint32_t v,
        uint64_t *rem)
{
    const uint32_t w[4] = { (uint32_t)lo, (uint32_t)(lo >> 32U),
            (uint32_t)hi, (uint32_t)(hi >> 32U) };
    uint64_t q[4] = { 0U, 0U, 0U, 0U };
    uint64_t r = 0U;
    for (int i = 3; i >= 0; i--) {
        const uint64_t cur = (r << 32U) | (uint64_t)w[i];
        q[i] = cur / (uint64_t)v;
        r = cur % (uint64_t)v;
    }
    if (rem != NULL) {
        *rem = r;
    }
    /* hi < v ⇒ q[3] == q[2] == 0，商恰为 64 位 */
    return (q[1] << 32U) | q[0];
}

/*
 * brief: 32 位前导零个数（0 返回 32）
 */
static unsigned u128_clz32(uint32_t x)
{
    unsigned n = 0U;
    if (x == 0U) {
        return 32U;
    }
    while ((x & UINT32_C(0x80000000)) == 0U) {
        x <<= 1U;
        n++;
    }
    return n;
}

/*
 * brief: 便携 128/64 除法（以 2^32 为基的 Knuth Algorithm D）
 * param: u1 高 64 位；要求 u1 < v
 * param: u0 低 64 位
 * param: v 除数（非零）
 * param: rem 传出余数
 * return: 商 (u1·2^64 + u0) / v
 * note: 被除数 4 个 32 位字、除数 2 个 32 位字（n=2, m=2）；
 *       中间乘积均为 32×32→64，故不依赖 128 位类型。
 */
static uint64_t u128_divmod_portable(uint64_t u1, uint64_t u0, uint64_t v,
        uint64_t *rem)
{
    if (v < (UINT64_C(1) << 32U)) {
        return u128_divmod_u32(u1, u0, (uint32_t)v, rem);
    }

    const uint32_t w[4] = { (uint32_t)u0, (uint32_t)(u0 >> 32U),
            (uint32_t)u1, (uint32_t)(u1 >> 32U) };
    const unsigned s = u128_clz32((uint32_t)(v >> 32U));
    uint32_t a[5];
    uint32_t b[2];
    uint32_t q[3] = { 0U, 0U, 0U };

    /* 规范化：x' = x << s，使除数最高位为 1（v << s 仍 < 2^64） */
    uint64_t carry = 0U;
    for (size_t i = 0U; i < 4U; i++) {
        const uint64_t t = ((uint64_t)w[i] << s) | carry;
        a[i] = (uint32_t)t;
        carry = t >> 32U;
    }
    a[4] = (uint32_t)carry;
    const uint64_t bv = v << s;
    b[0] = (uint32_t)bv;
    b[1] = (uint32_t)(bv >> 32U);

    const uint64_t base = UINT64_C(1) << 32U;
    size_t j = 3U;
    while (j > 0U) {
        j--;
        /* D3 试商 */
        const uint64_t num = ((uint64_t)a[j + 2U] << 32U) | a[j + 1U];
        uint64_t qhat = num / b[1];
        uint64_t rhat = num % b[1];
        while ((qhat >= base) || (qhat * b[0] > (rhat << 32U) + a[j])) {
            qhat--;
            rhat += b[1];
            if (rhat >= base) {
                break;
            }
        }
        /* D4 乘减（同 32 位肢 bin 的符号借位形式） */
        int64_t k = 0;
        for (size_t i = 0U; i < 2U; i++) {
            const uint64_t p = qhat * (uint64_t)b[i];
            const int64_t t = (int64_t)(uint64_t)a[j + i] - k
                    - (int64_t)(uint32_t)p;
            a[j + i] = (uint32_t)t;
            k = (int64_t)(p >> 32U) - (t >> 32U);
        }
        const int64_t t = (int64_t)(uint64_t)a[j + 2U] - k;
        a[j + 2U] = (uint32_t)t;
        /* D6 修正 */
        if (t < 0) {
            qhat--;
            uint64_t c = 0U;
            for (size_t i = 0U; i < 2U; i++) {
                const uint64_t sum = (uint64_t)a[j + i] + b[i] + c;
                a[j + i] = (uint32_t)sum;
                c = sum >> 32U;
            }
            a[j + 2U] = (uint32_t)((uint64_t)a[j + 2U] + c);
        }
        q[j] = (uint32_t)qhat;
    }

    /* D8 反规范化余数 */
    uint32_t r0;
    uint32_t r1;
    if (s == 0U) {
        r0 = a[0];
        r1 = a[1];
    } else {
        r0 = (a[0] >> s) | (a[1] << (32U - s));
        r1 = (a[1] >> s) | (a[2] << (32U - s));
    }
    if (rem != NULL) {
        *rem = ((uint64_t)r1 << 32U) | r0;
    }
    /* hi < v ⇒ 商 < 2^64，q[2] 恒为 0 */
    return ((uint64_t)q[1] << 32U) | q[0];
}

uint64_t nex_u128_divmod(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    return u128_divmod_portable(hi, lo, d, rem);
}
#else
/*
 * 硬件路径：GCC / Clang 的 128 位扩展。-Wpedantic 下局部抑制诊断
 * （仅此分支；MSVC 无 __SIZEOF_INT128__，走便携路径）。
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
uint64_t nex_u128_divmod(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    const unsigned __int128 n = ((unsigned __int128)hi << 64U) | lo;
    if (rem != NULL) {
        *rem = (uint64_t)(n % d);
    }
    return (uint64_t)(n / d);
}
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------ */
/* 内部辅助：单肢除法                                                    */
/* ------------------------------------------------------------------ */

/*
 * brief: quot = |lhs| / divisor，rem = |lhs| % divisor（divisor 非零）
 * note: 要求 |lhs| >= divisor；quot 可为 lhs 本身（就地）
 */
static bigint_err_ty divmod_single(bigint_bin64_ty *quot, bigint_bin64_ty *rem,
        const bigint_bin64_ty *lhs, uint64_t divisor)
{
    bigint_err_ty err = d64_ensure(quot, lhs->len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    uint64_t r = 0U;
    size_t i = lhs->len;
    while (i > 0U) {
        i--;
        quot->limbs[i] = nex_u128_divmod(r, lhs->limbs[i], divisor, &r);
    }
    quot->len = lhs->len;
    quot->sign = BIGINT_SIGN_POS_E;
    d64_normalize(quot);
    return d64_set_u64(rem, r);
}

/* ------------------------------------------------------------------ */
/* Knuth Algorithm D（基 2^64）                                          */
/* ------------------------------------------------------------------ */

/*
 * brief: 多肢幅值除法 quot = |lhs| / |rhs|，rem = |lhs| % |rhs|
 * note: 要求 |lhs| >= |rhs| 且 rhs 非零（由 API 层保证）
 */
static bigint_err_ty mag_divmod(bigint_bin64_ty *quot, bigint_bin64_ty *rem,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs)
{
    const size_t n = rhs->len;
    const size_t m = lhs->len - n;

    if (n == 1U) {
        return divmod_single(quot, rem, lhs, rhs->limbs[0]);
    }
    if ((m == 0U) && (d64_cmp_abs(lhs, rhs) == 0)) {
        /* 齐长且相等：商 = 1，余 = 0 */
        bigint_err_ty err = d64_set_u64(quot, UINT64_C(1));
        if (err != BIGINT_OK_E) {
            return err;
        }
        return d64_set_u64(rem, 0U);
    }

    /* D1 规范化：取 s 使除数最高肢最高位为 1 */
    const unsigned shift = d64_clz(rhs->limbs[n - 1U]);

    const size_t u_len = m + n + 1U;
    if ((u_len > SIZE_MAX / sizeof(uint64_t))
            || (n > SIZE_MAX / sizeof(uint64_t))) {
        return BIGINT_ERR_OOM_E;
    }
    uint64_t *u = (uint64_t *)nex_malloc(u_len * sizeof(uint64_t));
    if (u == NULL) {
        return BIGINT_ERR_OOM_E;
    }
    uint64_t *v = (uint64_t *)nex_malloc(n * sizeof(uint64_t));
    if (v == NULL) {
        free(u);
        return BIGINT_ERR_OOM_E;
    }

    /* u' = |lhs| << shift，长度 m + n + 1 */
    if (shift > 0U) {
        uint64_t carry = 0U;
        for (size_t i = 0U; i < lhs->len; i++) {
            u[i] = (lhs->limbs[i] << shift) | carry;
            carry = lhs->limbs[i] >> (64U - shift);
        }
        u[lhs->len] = carry;
    } else {
        memcpy(u, lhs->limbs, lhs->len * sizeof(uint64_t));
        u[lhs->len] = 0U;
    }

    /* v' = |rhs| << shift，长度 n */
    if (shift > 0U) {
        for (size_t i = 0U; i < n; i++) {
            const uint64_t cur = rhs->limbs[i] << shift;
            const uint64_t prev = (i > 0U)
                    ? (rhs->limbs[i - 1U] >> (64U - shift)) : 0U;
            v[i] = cur | prev;
        }
    } else {
        memcpy(v, rhs->limbs, n * sizeof(uint64_t));
    }

    const uint64_t vtop = v[n - 1U];
    const uint64_t vnext = v[n - 2U];

    bigint_err_ty err = d64_ensure(quot, m + 1U);
    if (err != BIGINT_OK_E) {
        free(u);
        free(v);
        return err;
    }
    err = d64_ensure(rem, n);
    if (err != BIGINT_OK_E) {
        free(u);
        free(v);
        return err;
    }

    /* D2 主循环：j = m .. 0 */
    size_t j = m + 1U;
    while (j > 0U) {
        j--;

        /* D3 试商 qhat（含 D6 前的预修正） */
        uint64_t qhat;
        uint64_t rhat;
        if (u[j + n] >= vtop) {
            /* 仅可能相等（余数 < 除数的不变量）；取基 − 1 */
            qhat = UINT64_MAX;
            rhat = u[j + n - 1U] + vtop;
            if (rhat < vtop) {
                /* rhat 上溢（≥ 2^64）：修正条件必不成立 */
                rhat = UINT64_MAX;
            } else {
                for (;;) {
                    const nex_u128_ty p = nex_u128_mul(qhat, vnext);
                    if ((p.hi < rhat)
                            || ((p.hi == rhat)
                                && (p.lo <= u[j + n - 2U]))) {
                        break;
                    }
                    qhat--;
                    const uint64_t next = rhat + vtop;
                    if (next < rhat) {
                        break;  /* rhat 上溢：停止修正 */
                    }
                    rhat = next;
                }
            }
        } else {
            qhat = nex_u128_divmod(u[j + n], u[j + n - 1U], vtop, &rhat);
            for (;;) {
                const nex_u128_ty p = nex_u128_mul(qhat, vnext);
                if ((p.hi < rhat)
                        || ((p.hi == rhat)
                            && (p.lo <= u[j + n - 2U]))) {
                    break;
                }
                qhat--;
                const uint64_t next = rhat + vtop;
                if (next < rhat) {
                    break;
                }
                rhat = next;
            }
        }

        /* D4 乘减：u[j .. j+n] -= qhat × v */
        uint64_t carry = 0U;
        uint64_t borrow = 0U;
        for (size_t i = 0U; i < n; i++) {
            const nex_u128_ty p = nex_u128_mul(qhat, v[i]);
            const uint64_t plo = p.lo + carry;
            carry = p.hi + ((plo < p.lo) ? 1U : 0U);

            const uint64_t ui = u[j + i];
            const uint64_t t = ui - plo;
            const uint64_t b1 = (ui < plo) ? 1U : 0U;
            const uint64_t t2 = t - borrow;
            const uint64_t b2 = (t < borrow) ? 1U : 0U;
            u[j + i] = t2;
            borrow = b1 | b2;
        }

        /* D5 顶肢与 D6 修正 */
        const uint64_t ui = u[j + n];
        const uint64_t t = ui - carry;
        const uint64_t b1 = (ui < carry) ? 1U : 0U;
        const uint64_t t2 = t - borrow;
        const uint64_t b2 = (t < borrow) ? 1U : 0U;
        u[j + n] = t2;
        if ((b1 | b2) != 0U) {
            qhat--;
            uint64_t c = 0U;
            for (size_t i = 0U; i < n; i++) {
                const uint64_t s1 = u[j + i] + v[i];
                const uint64_t c1 = (s1 < v[i]) ? 1U : 0U;
                const uint64_t s2 = s1 + c;
                const uint64_t c2 = (s2 < s1) ? 1U : 0U;
                u[j + i] = s2;
                c = c1 | c2;
            }
            u[j + n] += c;
        }

        quot->limbs[j] = qhat;
    }
    quot->len = m + 1U;
    quot->sign = BIGINT_SIGN_POS_E;
    d64_normalize(quot);

    /* D8 反规范化：余数 = 被除数低 n 肢 >> shift */
    if (shift > 0U) {
        for (size_t i = 0U; i < n; i++) {
            const uint64_t lo = u[i] >> shift;
            const uint64_t hi = (i + 1U < n)
                    ? (u[i + 1U] << (64U - shift)) : 0U;
            rem->limbs[i] = lo | hi;
        }
    } else {
        memcpy(rem->limbs, u, n * sizeof(uint64_t));
    }
    rem->len = n;
    rem->sign = BIGINT_SIGN_POS_E;
    d64_normalize(rem);

    free(u);
    free(v);
    return BIGINT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 超大输入：转 32 位肢走 bin 的 BZ 递归除法                             */
/* ------------------------------------------------------------------ */

/* 内部：bin64 → bin（32 位肢，小端拆分） */
static bigint_err_ty d64_bin64_to_bin(bigint_bin_ty *dst,
        const bigint_bin64_ty *src)
{
    (void)bigint_bin_init(dst);
    if (src->len > SIZE_MAX / 2U) {
        return BIGINT_ERR_OOM_E;
    }
    const size_t limbs32 = src->len * 2U;
    if (limbs32 > 0U) {
        bigint_err_ty err = bigint_bin_init_cap(dst, limbs32);
        if (err != BIGINT_OK_E) {
            return err;
        }
        for (size_t i = 0U; i < src->len; i++) {
            dst->limbs[2U * i] = (uint32_t)src->limbs[i];
            dst->limbs[2U * i + 1U] = (uint32_t)(src->limbs[i] >> 32U);
        }
        dst->len = limbs32;
        while ((dst->len > 0U) && (dst->limbs[dst->len - 1U] == 0U)) {
            dst->len--;
        }
    }
    dst->sign = (dst->len > 0U) ? src->sign : BIGINT_SIGN_ZERO_E;
    return BIGINT_OK_E;
}

/* 内部：bin（32 位肢）→ bin64，小端合并 */
static bigint_err_ty d64_bin_to_bin64(bigint_bin64_ty *dst,
        const bigint_bin_ty *src)
{
    const size_t limbs64 = (src->len + 1U) / 2U;
    (void)bigint_bin64_init(dst);
    if (limbs64 > 0U) {
        bigint_err_ty err = bigint_bin64_init_cap(dst, limbs64);
        if (err != BIGINT_OK_E) {
            return err;
        }
        for (size_t i = 0U; i < limbs64; i++) {
            const uint64_t lo = (2U * i < src->len)
                    ? (uint64_t)src->limbs[2U * i] : 0U;
            const uint64_t hi = (2U * i + 1U < src->len)
                    ? (uint64_t)src->limbs[2U * i + 1U] : 0U;
            dst->limbs[i] = lo | (hi << 32U);
        }
        dst->len = limbs64;
        while ((dst->len > 0U) && (dst->limbs[dst->len - 1U] == 0U)) {
            dst->len--;
        }
    }
    dst->sign = (dst->len > 0U) ? src->sign : BIGINT_SIGN_ZERO_E;
    return BIGINT_OK_E;
}

/* 转换包装：舍弃原生 Knuth D，复用 bin 的 BZ（结果写入 quot/rem） */
static bigint_err_ty div_rem_via_bin(bigint_bin64_ty *quot,
        bigint_bin64_ty *rem, const bigint_bin64_ty *lhs,
        const bigint_bin64_ty *rhs)
{
    bigint_bin_ty a32;
    bigint_bin_ty b32;
    bigint_bin_ty q32;
    bigint_bin_ty r32;
    (void)bigint_bin_init(&a32);
    (void)bigint_bin_init(&b32);
    (void)bigint_bin_init(&q32);
    (void)bigint_bin_init(&r32);

    bigint_err_ty err = d64_bin64_to_bin(&a32, lhs);
    if (err == BIGINT_OK_E) {
        err = d64_bin64_to_bin(&b32, rhs);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_div_rem(&q32, &r32, &a32, &b32);
    }
    if (err == BIGINT_OK_E) {
        err = d64_bin_to_bin64(quot, &q32);
    }
    if (err == BIGINT_OK_E) {
        err = d64_bin_to_bin64(rem, &r32);
    }

    bigint_bin_free(&r32);
    bigint_bin_free(&q32);
    bigint_bin_free(&b32);
    bigint_bin_free(&a32);
    return err;
}

/* ------------------------------------------------------------------ */
/* 对外 API                                                             */
/* ------------------------------------------------------------------ */

bigint_err_ty bigint_bin64_div_rem(bigint_bin64_ty *quot,
        bigint_bin64_ty *rem, const bigint_bin64_ty *lhs,
        const bigint_bin64_ty *rhs)
{
    if ((lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if (rhs->len == 0U) {
        return BIGINT_ERR_DIV_ZERO_E;
    }

    bigint_bin64_ty tq;
    bigint_bin64_ty tr;
    bigint_err_ty err = bigint_bin64_init(&tq);
    if (err != BIGINT_OK_E) {
        return err;
    }
    err = bigint_bin64_init(&tr);
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&tq);
        return err;
    }

    if (d64_cmp_abs(lhs, rhs) < 0) {
        /* |lhs| < |rhs|：商 0，余数拷贝被除数（带符号） */
        err = bigint_bin64_copy(&tr, lhs);
        if (err == BIGINT_OK_E) {
            err = d64_set_u64(&tq, 0U);
        }
    } else {
        /* 超大输入走 bin 的 BZ；其余走原生 64 位肢 Knuth D */
        if ((rhs->len >= NEX_BIN64_DIV_BZ_THRESHOLD)
                && (lhs->len >= 2U * rhs->len)) {
            err = div_rem_via_bin(&tq, &tr, lhs, rhs);
        } else {
            err = mag_divmod(&tq, &tr, lhs, rhs);
        }
    }
    if (err != BIGINT_OK_E) {
        bigint_bin64_free(&tq);
        bigint_bin64_free(&tr);
        return err;
    }

    /* 符号：商与余数与 C99 截断除法一致 */
    if (tq.len > 0U) {
        tq.sign = (lhs->sign == rhs->sign)
                ? BIGINT_SIGN_POS_E : BIGINT_SIGN_NEG_E;
    }
    if (tr.len > 0U) {
        tr.sign = lhs->sign;
    }

    if (quot != NULL) {
        bigint_bin64_move(quot, &tq);
    } else {
        bigint_bin64_free(&tq);
    }
    if (rem != NULL) {
        bigint_bin64_move(rem, &tr);
    } else {
        bigint_bin64_free(&tr);
    }
    return BIGINT_OK_E;
}
