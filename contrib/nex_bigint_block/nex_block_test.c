/* nex_block_test：分块 NTT 乘法与直接乘法的逐位对比（P1，issue #98）。 */

#include "nex_bigint_block.h"

#include "nex/bigint/bin/nex_bigint_bin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(c) do { \
    if (!(c)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        g_fail++; \
    } \
} while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd64(void)
{
    g_rng ^= g_rng << 13U;
    g_rng ^= g_rng >> 7U;
    g_rng ^= g_rng << 17U;
    return g_rng;
}

static void make_operand(bigint_bin_ty *v, size_t limbs)
{
    (void)bigint_bin_free(v);
    CHECK(bigint_bin_init_cap(v, limbs) == BIGINT_OK_E);
    for (size_t i = 0U; i < limbs; i++) {
        v->limbs[i] = (uint32_t)rnd64();
    }
    v->limbs[limbs - 1U] |= 0x80000000U;
    v->len = limbs;
    v->sign = BIGINT_SIGN_POS_E;
}

/* 大数 × 小乘数：直接 schoolbook O(n) 可作独立参考，用于验证深递归 */
static void check_large_small(double digits, uint64_t mul)
{
    const size_t limbs = (size_t)ceil(digits * log2(10.0) / 32.0);
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty direct;
    bigint_bin_ty block;
    (void)bigint_bin_init(&a);
    (void)bigint_bin_init(&b);
    (void)bigint_bin_init(&direct);
    (void)bigint_bin_init(&block);
    make_operand(&a, limbs);
    CHECK(bigint_bin_from_u64(&b, mul) == BIGINT_OK_E);

    CHECK(bigint_bin_mul(&direct, &a, &b) == BIGINT_OK_E);
    CHECK(nex_block_bin_mul(&block, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&direct, &block) == 0);

    a.sign = BIGINT_SIGN_NEG_E;
    CHECK(bigint_bin_mul(&direct, &a, &b) == BIGINT_OK_E);
    CHECK(nex_block_bin_mul(&block, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&direct, &block) == 0);

    printf("digits≈%.6g limbs=%zu × %llu: %s\n", digits, limbs,
            (unsigned long long)mul, (g_fail == 0) ? "ok" : "FAIL");
    bigint_bin_free(&block);
    bigint_bin_free(&direct);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

static void check_size(double digits)
{
    const size_t limbs = (size_t)ceil(digits * log2(10.0) / 32.0);
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty direct;
    bigint_bin_ty block;
    (void)bigint_bin_init(&a);
    (void)bigint_bin_init(&b);
    (void)bigint_bin_init(&direct);
    (void)bigint_bin_init(&block);
    if (limbs > (size_t)1U << 26U) {
        bigint_bin_free(&block);
        bigint_bin_free(&direct);
        bigint_bin_free(&b);
        bigint_bin_free(&a);
        check_large_small(digits, 123456789ULL);
        return;
    }
    make_operand(&a, limbs);
    make_operand(&b, limbs);

    CHECK(bigint_bin_mul(&direct, &a, &b) == BIGINT_OK_E);
    CHECK(nex_block_bin_mul(&block, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&direct, &block) == 0);

    /* 负数与零的边界 */
    a.sign = BIGINT_SIGN_NEG_E;
    CHECK(bigint_bin_mul(&direct, &a, &b) == BIGINT_OK_E);
    CHECK(nex_block_bin_mul(&block, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&direct, &block) == 0);
    bigint_bin_from_u64(&a, 0U);
    CHECK(nex_block_bin_mul(&block, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&block));

    printf("digits≈%.6g limbs=%zu: %s\n", digits, limbs,
            (g_fail == 0) ? "ok" : "FAIL");
    bigint_bin_free(&block);
    bigint_bin_free(&direct);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            check_size(strtod(argv[i], NULL));
        }
    } else {
        check_size(1e6);
        check_size(1e7);
    }
    if (g_fail == 0) {
        printf("block NTT: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
