/* 单元测试：bigint_bin64 64 位肢大整数（设计文档 §13 #3） */
#include "nex/bigint/bin64/nex_bigint_bin64.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include <stdio.h>
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

/* ------------------------------------------------------------------ */
/* 随机交叉验证：bin64 运算 vs bigint_bin（同值同结果）                  */
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

int main(void)
{
    test_u128();
    test_cross_validate();
    test_edge();
    if (g_fail == 0) {
        printf("bigint_bin64: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
