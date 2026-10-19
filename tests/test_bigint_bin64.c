/* 单元测试：bigint_bin64 64 位肢大整数（设计文档 §13 #3） */
#include "nex/bigint/bin64/nex_bigint_bin64.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

static uint32_t g_rng = 0xdeadbeefU;
static uint32_t next_u32(void)
{
    g_rng = g_rng * 1664525U + 1013904223U;
    return g_rng;
}

/* 等值互转：bin64 ↔ bin（64 位肢 ↔ 32 位半字） */
static void to_bin(bigint_bin_ty *out, const bigint_bin64_ty *v)
{
    if (v->len == 0U) {
        out->len = 0U;
        out->sign = BIGINT_SIGN_ZERO_E;
        return;
    }
    const size_t n = v->len * 2U;
    bigint_bin_init_cap(out, n);
    for (size_t i = 0U; i < v->len; i++) {
        out->limbs[2U * i] = (uint32_t)v->limbs[i];
        out->limbs[2U * i + 1U] = (uint32_t)(v->limbs[i] >> 32U);
    }
    out->len = n;
    out->sign = v->sign;
    /* 去尾零肢（最高 32 位半字可能为零） */
    while ((out->len > 0U) && (out->limbs[out->len - 1U] == 0U)) {
        out->len--;
    }
    if (out->len == 0U) {
        out->sign = BIGINT_SIGN_ZERO_E;
    }
}

static void from_bin(bigint_bin64_ty *out, const bigint_bin_ty *v)
{
    bigint_bin64_init(out);
    if (v->len == 0U) {
        return;
    }
    const size_t n = (v->len + 1U) / 2U;
    bigint_bin64_init_cap(out, n);
    for (size_t i = 0U; i < n; i++) {
        const uint64_t lo = (uint64_t)v->limbs[2U * i];
        const uint64_t hi = (2U * i + 1U < v->len)
                ? (uint64_t)v->limbs[2U * i + 1U] : 0U;
        out->limbs[i] = lo | (hi << 32U);
    }
    out->len = n;
    out->sign = v->sign;
    while ((out->len > 0U) && (out->limbs[out->len - 1U] == 0U)) {
        out->len--;
    }
    if (out->len == 0U) {
        out->sign = BIGINT_SIGN_ZERO_E;
    }
}

/* ------------------------------------------------------------------ */
/* 128 位乘法向量（Python 生成，硬编码）                                 */
/* ------------------------------------------------------------------ */

static void test_u128(void)
{
    static const struct {
        uint64_t a;
        uint64_t b;
        uint64_t lo;
        uint64_t hi;
    } vectors[] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL },
        { 0x0000000000000001ULL, 0x0000000000000001ULL, 0x0000000000000001ULL, 0x0000000000000000ULL },
        { 0xFFFFFFFFFFFFFFFFULL, 0x0000000000000001ULL, 0xFFFFFFFFFFFFFFFFULL, 0x0000000000000000ULL },
        { 0x00000000FFFFFFFFULL, 0x0000000100000000ULL, 0xFFFFFFFF00000000ULL, 0x0000000000000000ULL },
        { 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0x0000000000000001ULL, 0xFFFFFFFFFFFFFFFEULL },
        { 0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL, 0x236D88FE5618CF00ULL, 0x121FA00AD77D7422ULL },
        { 0x8000000000000000ULL, 0x8000000000000000ULL, 0x0000000000000000ULL, 0x4000000000000000ULL },
        { 0xDEADBEEFCAFEBABEULL, 0x0123456789ABCDEFULL, 0x7EB689F4EA447D62ULL, 0x00FD5BDEEEB2A01DULL },
    };
    for (size_t i = 0U; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        const nex_u128_ty r = nex_u128_mul(vectors[i].a, vectors[i].b);
        if ((r.lo != vectors[i].lo) || (r.hi != vectors[i].hi)) {
            printf("FAIL u128[%u]: 0x%016llX×0x%016llX got %016llX%016llX\n",
                    (unsigned)i, (unsigned long long)vectors[i].a,
                    (unsigned long long)vectors[i].b,
                    (unsigned long long)r.hi, (unsigned long long)r.lo);
            g_fail++;
        }
    }
    /* 随机抽查：与 32 位半字重算比对 */
    for (int t = 0; t < 200; t++) {
        const uint64_t a = ((uint64_t)next_u32() << 32) | next_u32();
        const uint64_t b = ((uint64_t)next_u32() << 32) | next_u32();
        const nex_u128_ty r = nex_u128_mul(a, b);
        const uint64_t a0 = (uint32_t)a;
        const uint64_t a1 = a >> 32U;
        const uint64_t b0 = (uint32_t)b;
        const uint64_t b1 = b >> 32U;
        const uint64_t p00 = a0 * b0;
        const uint64_t p01 = a0 * b1;
        const uint64_t p10 = a1 * b0;
        const uint64_t p11 = a1 * b1;
        const uint64_t mid = (p00 >> 32U) + (uint32_t)p01 + (uint32_t)p10;
        const uint64_t want_lo = (p00 & 0xFFFFFFFFU) | (mid << 32U);
        const uint64_t want_hi = p11 + (p01 >> 32U) + (p10 >> 32U)
                + (mid >> 32U);
        if ((r.lo != want_lo) || (r.hi != want_hi)) {
            printf("FAIL u128 random\n");
            g_fail++;
            break;
        }
    }
}

/* 128/64 除法：q·d + r == (hi·2^64 + lo) 且 r < d（唯一性 ⇒ 正确性） */
static void check_u128_div(uint64_t hi, uint64_t lo, uint64_t d)
{
    uint64_t rem = 0U;
    const uint64_t q = nex_u128_divmod(hi, lo, d, &rem);
    CHECK(rem < d);
    const nex_u128_ty p = nex_u128_mul(q, d);
    const uint64_t lo2 = p.lo + rem;
    const uint64_t hi2 = p.hi + ((lo2 < p.lo) ? 1U : 0U);
    if ((lo2 != lo) || (hi2 != hi)) {
        printf("FAIL u128_div hi=%016llx lo=%016llx d=%016llx\n",
                (unsigned long long)hi, (unsigned long long)lo,
                (unsigned long long)d);
        g_fail++;
    }
}

static void test_u128_div(void)
{
    static const uint64_t ds[] = { 1U, 2U, 3U, 0xFFFFFFFFU,
            UINT64_C(0x100000000), UINT64_C(0x123456789ABCDEF),
            UINT64_C(0x7FFFFFFFFFFFFFFF), UINT64_C(0x8000000000000000),
            UINT64_C(0xFFFFFFFFFFFFFFFE), UINT64_MAX };
    for (size_t i = 0U; i < sizeof(ds) / sizeof(ds[0]); i++) {
        const uint64_t d = ds[i];
        check_u128_div(0U, 0U, d);
        check_u128_div(0U, UINT64_MAX, d);
        check_u128_div(d - 1U, UINT64_MAX, d);
        for (int t = 0; t < 50; t++) {
            const uint64_t lo = ((uint64_t)next_u32() << 32U) | next_u32();
            const uint64_t hi = (((uint64_t)next_u32() << 32U)
                    | next_u32()) % d;
            check_u128_div(hi, lo, d);
        }
    }
}

/* ------------------------------------------------------------------ */

static void rand_bin(bigint_bin_ty *v, size_t len)
{
    bigint_bin_init_cap(v, len);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = next_u32();
    }
    if (len > 0U) {
        v->limbs[len - 1U] |= 0x80000000U;
    }
    v->len = len;
    v->sign = (next_u32() & 1U) ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
}

static void check_op(const char *op, size_t la, size_t lb)
{
    bigint_bin_ty a, b, r32, tmp;
    bigint_bin64_ty a64, b64, r64;
    rand_bin(&a, la);
    rand_bin(&b, lb);
    bigint_bin_init(&r32);
    bigint_bin_init(&tmp);
    from_bin(&a64, &a);
    from_bin(&b64, &b);
    bigint_bin64_init(&r64);

    if (op[0] == 'a') {
        CHECK(bigint_bin64_add(&r64, &a64, &b64) == BIGINT_OK_E);
        CHECK(bigint_bin_add(&r32, &a, &b) == BIGINT_OK_E);
    } else if (op[0] == 's') {
        CHECK(bigint_bin64_sub(&r64, &a64, &b64) == BIGINT_OK_E);
        CHECK(bigint_bin_sub(&r32, &a, &b) == BIGINT_OK_E);
    } else {
        CHECK(bigint_bin64_mul(&r64, &a64, &b64) == BIGINT_OK_E);
        CHECK(bigint_bin_mul(&r32, &a, &b) == BIGINT_OK_E);
    }
    to_bin(&tmp, &r64);
    if (bigint_bin_cmp(&r32, &tmp) != 0) {
        printf("FAIL %s cross la=%u lb=%u\n", op, (unsigned)la, (unsigned)lb);
        g_fail++;
    }

    bigint_bin64_free(&r64);
    bigint_bin_free(&tmp);
    bigint_bin_free(&r32);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
    bigint_bin64_free(&b64);
    bigint_bin64_free(&a64);
}

static void test_cross_validate(void)
{
    static const size_t lens[] = { 1U, 2U, 3U, 4U, 7U, 8U, 15U, 16U, 17U,
            31U, 32U, 33U, 63U, 64U, 65U, 100U, 127U, 128U, 129U, 200U };
    for (size_t i = 0U; i < sizeof(lens) / sizeof(lens[0]); i++) {
        for (size_t j = 0U; j < sizeof(lens) / sizeof(lens[0]); j += 7U) {
            check_op("add", lens[i], lens[j]);
            check_op("sub", lens[i], lens[j]);
            check_op("mul", lens[i], lens[j]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 边界与别名                                                           */
/* ------------------------------------------------------------------ */

static void test_edge(void)
{
    bigint_bin64_ty a, b, r, zero;
    bigint_bin64_init(&a);
    bigint_bin64_init(&b);
    bigint_bin64_init(&r);
    bigint_bin64_init(&zero);

    /* 零 */
    CHECK(bigint_bin64_is_zero(&zero));
    CHECK(bigint_bin64_mul(&r, &zero, &zero) == BIGINT_OK_E);
    CHECK(bigint_bin64_is_zero(&r));
    CHECK(bigint_bin64_add(&r, &zero, &zero) == BIGINT_OK_E);
    CHECK(bigint_bin64_is_zero(&r));

    /* u64 边界 */
    CHECK(bigint_bin64_from_u64(&a, 0xFFFFFFFFFFFFFFFFULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_from_u64(&b, 2ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_mul(&r, &a, &b) == BIGINT_OK_E);
    {
        uint64_t lo = 0U;
        uint64_t hi = 0U;
        /* 0xFFFFFFFFFFFFFFFF × 2 = 0x1FFFFFFFFFFFFFFFE */
        const bigint_bin64_ty *rp = &r;
        CHECK(rp->len == 2U);
        lo = rp->limbs[0];
        hi = rp->limbs[1];
        CHECK(lo == 0xFFFFFFFFFFFFFFFEULL);
        CHECK(hi == 1ULL);
    }

    /* 别名：dst 与操作数同一对象 */
    CHECK(bigint_bin64_from_u64(&a, 12345678901234567890ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_from_u64(&b, 9876543210987654321ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_mul(&a, &a, &b) == BIGINT_OK_E);
    /* 期望值：用 32 位交叉验证已覆盖，此处仅验证别名不崩且非零 */
    CHECK(!bigint_bin64_is_zero(&a));

    /* 负 × 负 = 正 */
    bigint_bin64_from_u64(&a, 100ULL);
    bigint_bin64_from_u64(&b, 200ULL);
    a.sign = BIGINT_SIGN_NEG_E;
    b.sign = BIGINT_SIGN_NEG_E;
    CHECK(bigint_bin64_mul(&r, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin64_cmp(&r, &zero) > 0);

    /* 负 - 负 = 0 */
    bigint_bin64_from_u64(&a, 100ULL);
    bigint_bin64_from_u64(&b, 100ULL);
    a.sign = BIGINT_SIGN_NEG_E;
    b.sign = BIGINT_SIGN_NEG_E;
    CHECK(bigint_bin64_sub(&r, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin64_is_zero(&r));

    bigint_bin64_free(&zero);
    bigint_bin64_free(&r);
    bigint_bin64_free(&b);
    bigint_bin64_free(&a);
}

/*
 * bin64 除法专项：LCG 随机 + 边界，验证不变量 q·b + r == a（纯 bin64 内）
 */
static void test_div(void)
{
    bigint_bin64_ty a, b, q, r, t;
    bigint_bin64_init(&a);
    bigint_bin64_init(&b);
    bigint_bin64_init(&q);
    bigint_bin64_init(&r);
    bigint_bin64_init(&t);

    /* 基本除法 */
    CHECK(bigint_bin64_from_u64(&a, 100ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_from_u64(&b, 7ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
    {
        uint64_t qv = 0U;
        uint64_t rv = 0U;
        CHECK(bigint_bin64_to_u64(&q, &qv) == BIGINT_OK_E);
        CHECK(bigint_bin64_to_u64(&r, &rv) == BIGINT_OK_E);
        CHECK(qv == 14ULL);
        CHECK(rv == 2ULL);
    }
    /* 除零 */
    CHECK(bigint_bin64_from_u64(&b, 0ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_ERR_DIV_ZERO_E);
    /* |lhs| < |rhs| */
    CHECK(bigint_bin64_from_u64(&a, 5ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_from_u64(&b, 10ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
    {
        uint64_t qv = 99U;
        uint64_t rv = 99U;
        CHECK(bigint_bin64_to_u64(&q, &qv) == BIGINT_OK_E);
        CHECK(bigint_bin64_to_u64(&r, &rv) == BIGINT_OK_E);
        CHECK(qv == 0ULL);
        CHECK(rv == 5ULL);
    }
    /* 负数：截断语义 */
    CHECK(bigint_bin64_from_u64(&a, 100ULL) == BIGINT_OK_E);
    CHECK(bigint_bin64_from_u64(&b, 7ULL) == BIGINT_OK_E);
    a.sign = BIGINT_SIGN_NEG_E;
    CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
    {
        uint64_t qv = 0U;
        CHECK(bigint_bin64_to_u64(&q, &qv) == BIGINT_OK_E);
        CHECK(qv == 14ULL);
        CHECK(q.sign == BIGINT_SIGN_NEG_E);
        CHECK(r.sign == BIGINT_SIGN_NEG_E);
    }

    /* LCG 随机：不变量 q·b + r == a */
    {
        uint32_t seed = 0x20240813U;
        size_t k;
        for (k = 0; k < 5; k++) {
            size_t bl = 8 + k * 30;
            size_t ql = 20 + k * 60;
            size_t i;
            bigint_bin64_ty q0;
            bigint_bin64_init(&q0);
            bigint_bin64_free(&b);   /* 复用 b：先释放上一轮 */
            CHECK(bigint_bin64_init_cap(&b, bl) == BIGINT_OK_E);
            CHECK(bigint_bin64_init_cap(&q0, ql) == BIGINT_OK_E);
            b.len = bl;
            q0.len = ql;
            for (i = 0; i < bl; i++) {
                seed = seed * 1664525U + 1013904223U;
                b.limbs[i] = ((uint64_t)seed << 32U) | (seed + 1U);
            }
            b.limbs[bl - 1U] |= 0x8000000000000000ULL;
            b.sign = BIGINT_SIGN_POS_E;
            for (i = 0; i < ql; i++) {
                seed = seed * 1664525U + 1013904223U;
                q0.limbs[i] = ((uint64_t)seed << 32U) | (seed + 1U);
            }
            q0.limbs[ql - 1U] |= 0x8000000000000000ULL;
            q0.sign = BIGINT_SIGN_POS_E;
            CHECK(bigint_bin64_mul(&a, &q0, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_add(&a, &a, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_sub(&a, &a, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_from_u64(&r, 7ULL) == BIGINT_OK_E);
            CHECK(bigint_bin64_add(&a, &a, &r) == BIGINT_OK_E);
            CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_cmp(&q, &q0) == 0);
            {
                uint64_t rv = 99U;
                CHECK(bigint_bin64_to_u64(&r, &rv) == BIGINT_OK_E);
                CHECK(rv == 7ULL);
            }
            /* 不变量 */
            CHECK(bigint_bin64_mul(&t, &q, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_add(&t, &t, &r) == BIGINT_OK_E);
            CHECK(bigint_bin64_cmp(&t, &a) == 0);
            bigint_bin64_free(&q0);
        }
    }

    bigint_bin64_free(&t);
    bigint_bin64_free(&r);
    bigint_bin64_free(&q);
    bigint_bin64_free(&b);
    bigint_bin64_free(&a);
}

/* ------------------------------------------------------------------ */
/* 除法：与 32 位肢 bin 的 Knuth D / BZ 交叉验证                        */
/* ------------------------------------------------------------------ */

static uint64_t next_u64(void)
{
    return ((uint64_t)next_u32() << 32U) | next_u32();
}

/* 随机幅值（最高肢置顶，保证给定肢数） */
static void rand_b64(bigint_bin64_ty *v, size_t len)
{
    bigint_bin64_free(v);
    bigint_bin64_init_cap(v, (len > 0U) ? len : 1U);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = next_u64();
    }
    if (len > 0U) {
        v->limbs[len - 1U] |= UINT64_C(0x8000000000000000);
        v->len = len;
        v->sign = BIGINT_SIGN_POS_E;
    }
}

static void check_div_cross(size_t la, size_t lb)
{
    bigint_bin64_ty a, b, q, r;
    bigint_bin_ty a32, b32, q32, r32;
    bigint_bin64_init(&a);
    bigint_bin64_init(&b);
    bigint_bin64_init(&q);
    bigint_bin64_init(&r);
    bigint_bin_init(&a32);
    bigint_bin_init(&b32);
    bigint_bin_init(&q32);
    bigint_bin_init(&r32);

    rand_b64(&a, la);
    rand_b64(&b, lb);
    CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);

    /* 同值转 32 位肢，走 bin 的（BZ / Knuth D）除法 */
    {
        bigint_bin64_ty tmp;
        bigint_bin64_init(&tmp);
        to_bin(&a32, &a);
        to_bin(&b32, &b);
        CHECK(bigint_bin_div_rem(&q32, &r32, &a32, &b32) == BIGINT_OK_E);
        from_bin(&tmp, &q32);
        CHECK(bigint_bin64_cmp(&q, &tmp) == 0);
        bigint_bin64_free(&tmp);
        bigint_bin64_init(&tmp);
        from_bin(&tmp, &r32);
        CHECK(bigint_bin64_cmp(&r, &tmp) == 0);
        bigint_bin64_free(&tmp);
    }

    /* 不变量 q·b + r == a 且 |r| < |b| */
    {
        bigint_bin64_ty t;
        bigint_bin64_ty rabs = r;
        bigint_bin64_ty babs = b;
        bigint_bin64_init(&t);
        CHECK(bigint_bin64_mul(&t, &q, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_add(&t, &t, &r) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&t, &a) == 0);
        bigint_bin64_free(&t);
        rabs.sign = (rabs.len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
        babs.sign = BIGINT_SIGN_POS_E;
        CHECK(bigint_bin64_cmp(&rabs, &babs) < 0);
    }

    bigint_bin_free(&r32);
    bigint_bin_free(&q32);
    bigint_bin_free(&b32);
    bigint_bin_free(&a32);
    bigint_bin64_free(&r);
    bigint_bin64_free(&q);
    bigint_bin64_free(&b);
    bigint_bin64_free(&a);
}

static void test_div_cross(void)
{
    static const size_t lens[] = { 1U, 2U, 3U, 5U, 8U, 16U, 31U, 32U, 33U,
            47U, 63U, 64U, 65U, 80U, 100U, 128U, 200U };
    for (size_t i = 0U; i < sizeof(lens) / sizeof(lens[0]); i++) {
        for (size_t j = 0U; j < sizeof(lens) / sizeof(lens[0]); j += 3U) {
            check_div_cross(lens[i] + (size_t)(next_u32() % 7U),
                    lens[j] + (size_t)(next_u32() % 3U));
        }
    }
    /* 超过 BZ 回退阈值（1024 肢）的大输入 */
    check_div_cross(2U * 1200U + 5U, 1200U);
    check_div_cross(2U * 2048U + 3U, 2048U);
}

/* 对抗性模式：全 1 除数、精确倍数、2 的幂、单肢除数 */
static void test_div_adversarial(void)
{
    bigint_bin64_ty a, b, q, r, t;
    bigint_bin64_init(&a);
    bigint_bin64_init(&b);
    bigint_bin64_init(&q);
    bigint_bin64_init(&r);
    bigint_bin64_init(&t);

    for (size_t lb = 1U; lb <= 80U; lb++) {
        /* 除数全 1（触发 D6 修正的高概率形态） */
        rand_b64(&b, lb);
        for (size_t i = 0U; i < lb; i++) {
            b.limbs[i] = UINT64_MAX;
        }
        for (size_t trial = 0U; trial < 2U; trial++) {
            const size_t la = lb + 1U + (size_t)(next_u32() % (lb * 2U + 1U));
            rand_b64(&a, la);
            CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_mul(&t, &q, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_add(&t, &t, &r) == BIGINT_OK_E);
            CHECK(bigint_bin64_cmp(&t, &a) == 0);
            CHECK(bigint_bin64_cmp(&r, &b) < 0);
        }
        /* 精确倍数：a = b²，商 = b，余 = 0 */
        CHECK(bigint_bin64_mul(&a, &b, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&q, &b) == 0);
        CHECK(bigint_bin64_is_zero(&r));
        /* 精确倍数 + 1：商 = b，余 = 1 */
        CHECK(bigint_bin64_from_u64(&t, 1U) == BIGINT_OK_E);
        CHECK(bigint_bin64_add(&a, &a, &t) == BIGINT_OK_E);
        CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&q, &b) == 0);
        {
            uint64_t rv = 0U;
            CHECK(bigint_bin64_to_u64(&r, &rv) == BIGINT_OK_E);
            CHECK(rv == 1U);
        }
    }

    /* 2 的幂除数（含跨肢借位：2^64、2^65 等） */
    for (size_t k = 1U; k <= 40U; k++) {
        const size_t lb = (k / 64U) + 1U;
        rand_b64(&b, lb);
        for (size_t i = 0U; i < lb; i++) {
            b.limbs[i] = 0U;
        }
        b.limbs[k / 64U] = UINT64_C(1) << (k % 64U);
        b.len = (k / 64U) + 1U;
        b.sign = BIGINT_SIGN_POS_E;
        rand_b64(&a, lb + 3U);
        CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_mul(&t, &q, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_add(&t, &t, &r) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&t, &a) == 0);
        CHECK(bigint_bin64_cmp(&r, &b) < 0);
    }

    /* 单肢除数（走 divmod_single） */
    for (size_t la = 1U; la <= 120U; la += 7U) {
        rand_b64(&a, la);
        for (int t2 = 0; t2 < 3; t2++) {
            CHECK(bigint_bin64_from_u64(&b, next_u64() | 1U) == BIGINT_OK_E);
            CHECK(bigint_bin64_div_rem(&q, &r, &a, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_mul(&t, &q, &b) == BIGINT_OK_E);
            CHECK(bigint_bin64_add(&t, &t, &r) == BIGINT_OK_E);
            CHECK(bigint_bin64_cmp(&t, &a) == 0);
            CHECK(bigint_bin64_cmp(&r, &b) < 0);
        }
    }

    bigint_bin64_free(&t);
    bigint_bin64_free(&r);
    bigint_bin64_free(&q);
    bigint_bin64_free(&b);
    bigint_bin64_free(&a);
}

/* ------------------------------------------------------------------ */
/* 字符串 I/O                                                           */
/* ------------------------------------------------------------------ */

static void check_str_roundtrip(const bigint_bin64_ty *v, uint32_t base)
{
    size_t need = 0U;
    CHECK(bigint_bin64_to_str(v, base, NULL, 0U, &need) == BIGINT_OK_E);
    CHECK(need >= 2U);
    char *buf = (char *)malloc(need + 4U);
    CHECK(buf != NULL);
    if (buf == NULL) {
        return;
    }
    memset(buf, 0x7F, need + 4U);
    CHECK(bigint_bin64_to_str(v, base, buf, need, &need) == BIGINT_OK_E);
    CHECK(strlen(buf) + 1U == need);
    /* 缓冲区不足：OVERFLOW，needed 仍写出，内容不承诺 */
    {
        size_t need2 = 0U;
        CHECK(bigint_bin64_to_str(v, base, buf, need - 1U, &need2)
                == BIGINT_ERR_OVERFLOW_E);
        CHECK(need2 == need);
    }

    bigint_bin64_ty w;
    bigint_bin64_init(&w);
    const char *end = NULL;
    CHECK(bigint_bin64_from_str(&w, buf, base, &end) == BIGINT_OK_E);
    CHECK(end != NULL);
    CHECK(*end == '\0');
    CHECK(bigint_bin64_cmp(&w, v) == 0);
    bigint_bin64_free(&w);
    free(buf);
}

static void test_str(void)
{
    bigint_bin64_ty v;
    bigint_bin64_init(&v);

    /* 已知值 */
    CHECK(bigint_bin64_from_u64(&v, UINT64_MAX) == BIGINT_OK_E);
    {
        char buf[80];
        CHECK(bigint_bin64_to_str(&v, 10U, buf, sizeof(buf), NULL)
                == BIGINT_OK_E);
        CHECK(strcmp(buf, "18446744073709551615") == 0);
        CHECK(bigint_bin64_to_str(&v, 16U, buf, sizeof(buf), NULL)
                == BIGINT_OK_E);
        CHECK(strcmp(buf, "ffffffffffffffff") == 0);
        CHECK(bigint_bin64_to_str(&v, 2U, buf, sizeof(buf), NULL)
                == BIGINT_OK_E);
        CHECK(strcmp(buf,
                "1111111111111111111111111111111111111111111111111111111111111111")
                == 0);
    }

    /* 随机值 × 全部进制往返 */
    for (size_t len = 0U; len <= 48U; len++) {
        rand_b64(&v, len);
        if (len > 0U) {
            v.sign = (next_u32() & 1U) ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
        }
        for (uint32_t base = 2U; base <= 36U; base++) {
            check_str_roundtrip(&v, base);
        }
    }

    /* 前缀、部分消费与错误码 */
    {
        bigint_bin64_ty w;
        bigint_bin64_init(&w);
        const char *end = NULL;
        CHECK(bigint_bin64_from_str(&w, "0xFF", 16U, &end) == BIGINT_OK_E);
        CHECK(*end == '\0');
        {
            uint64_t x = 0U;
            CHECK(bigint_bin64_to_u64(&w, &x) == BIGINT_OK_E);
            CHECK(x == 255U);
        }
        CHECK(bigint_bin64_from_str(&w, "-0b1010", 2U, &end) == BIGINT_OK_E);
        CHECK(w.sign == BIGINT_SIGN_NEG_E);
        {
            uint64_t x = 0U;
            CHECK(bigint_bin64_to_u64(&w, &x) == BIGINT_OK_E);
            CHECK(x == 10U);
        }
        CHECK(bigint_bin64_from_str(&w, "12z", 10U, &end) == BIGINT_OK_E);
        CHECK(strcmp(end, "z") == 0);
        {
            uint64_t x = 0U;
            CHECK(bigint_bin64_to_u64(&w, &x) == BIGINT_OK_E);
            CHECK(x == 12U);
        }
        CHECK(bigint_bin64_from_str(&w, "", 10U, &end)
                == BIGINT_ERR_PARSE_E);
        CHECK(end != NULL && strcmp(end, "") == 0);
        CHECK(bigint_bin64_from_str(&w, "-", 10U, &end)
                == BIGINT_ERR_PARSE_E);
        CHECK(bigint_bin64_from_str(&w, "0x", 16U, &end)
                == BIGINT_ERR_PARSE_E);
        CHECK(bigint_bin64_from_str(&w, "123", 1U, &end)
                == BIGINT_ERR_INVALID_E);
        CHECK(bigint_bin64_from_str(&w, "123", 37U, &end)
                == BIGINT_ERR_INVALID_E);
        {
            char buf[8];
            CHECK(bigint_bin64_to_str(&w, 0U, buf, sizeof(buf), NULL)
                    == BIGINT_ERR_INVALID_E);
            CHECK(bigint_bin64_to_str(&w, 37U, buf, sizeof(buf), NULL)
                    == BIGINT_ERR_INVALID_E);
        }
        bigint_bin64_free(&w);
    }

    /* 负零规范化为零 */
    {
        bigint_bin64_ty w;
        bigint_bin64_init(&w);
        CHECK(bigint_bin64_from_str(&w, "-0", 10U, NULL) == BIGINT_OK_E);
        CHECK(bigint_bin64_is_zero(&w));
        CHECK(w.sign == BIGINT_SIGN_ZERO_E);
        bigint_bin64_free(&w);
    }

    bigint_bin64_free(&v);
}

/* ------------------------------------------------------------------ */
/* bin64 ↔ bin / dec 互转                                               */
/* ------------------------------------------------------------------ */

static void test_conv(void)
{
    bigint_bin64_ty v, w;
    bigint_bin64_init(&v);
    bigint_bin64_init(&w);

    for (size_t len = 1U; len <= 64U; len *= 2U) {
        rand_b64(&v, len);
        v.sign = (next_u32() & 1U) ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;

        bigint_bin_ty b;
        bigint_bin_init(&b);
        CHECK(bigint_conv_bin64_to_bin(&b, &v) == BIGINT_OK_E);
        CHECK(bigint_conv_bin_to_bin64(&w, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&v, &w) == 0);
        bigint_bin_free(&b);

        bigint_dec_ty d;
        bigint_dec_init(&d);
        CHECK(bigint_conv_bin64_to_dec(&d, &v) == BIGINT_OK_E);
        CHECK(bigint_conv_dec_to_bin64(&w, &d) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&v, &w) == 0);
        bigint_dec_free(&d);
    }

    /* 零与符号 */
    CHECK(bigint_bin64_from_u64(&v, 0U) == BIGINT_OK_E);
    {
        bigint_bin_ty b;
        bigint_bin_init(&b);
        CHECK(bigint_conv_bin64_to_bin(&b, &v) == BIGINT_OK_E);
        CHECK(b.len == 0U && b.sign == BIGINT_SIGN_ZERO_E);
        CHECK(bigint_conv_bin_to_bin64(&w, &b) == BIGINT_OK_E);
        CHECK(bigint_bin64_is_zero(&w));
        bigint_bin_free(&b);
    }
    CHECK(bigint_bin64_from_str(&v, "-123456789012345678901234567890",
            10U, NULL) == BIGINT_OK_E);
    CHECK(v.sign == BIGINT_SIGN_NEG_E);
    {
        bigint_dec_ty d;
        bigint_dec_init(&d);
        CHECK(bigint_conv_bin64_to_dec(&d, &v) == BIGINT_OK_E);
        CHECK(bigint_conv_dec_to_bin64(&w, &d) == BIGINT_OK_E);
        CHECK(bigint_bin64_cmp(&v, &w) == 0);
        bigint_dec_free(&d);
    }

    bigint_bin64_free(&w);
    bigint_bin64_free(&v);
}

int main(void)
{
    test_u128();
    test_u128_div();
    test_cross_validate();
    test_edge();
    test_div();
    test_div_cross();
    test_div_adversarial();
    test_str();
    test_conv();
    if (g_fail == 0) {
        printf("bigint_bin64: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
