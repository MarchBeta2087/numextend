/*
 * nex_parallel_test.c：并行 NTT 乘法自检。
 *
 * 对每个用例，比较 nex_parallel_bin_mul_ntt（1/4/自动线程）与核心
 * bigint_bin_mul 的结果，要求逐位相等（含符号）。覆盖随机大数、
 * 全 1、2 的幂、负数与零。
 */

#include "nex_parallel_ntt.h"
#include "nex_thread.h"

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

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd64(void)
{
    g_rng ^= g_rng << 13U;
    g_rng ^= g_rng >> 7U;
    g_rng ^= g_rng << 17U;
    return g_rng;
}

static void make_rand(bigint_bin_ty *v, size_t len)
{
    bigint_bin_free(v);
    CHECK(bigint_bin_init_cap(v, len) == BIGINT_OK_E);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = (uint32_t)rnd64();
    }
    v->limbs[len - 1U] |= 0x80000000U;
    v->len = len;
    v->sign = BIGINT_SIGN_POS_E;
}

static void make_ones(bigint_bin_ty *v, size_t len)
{
    bigint_bin_free(v);
    CHECK(bigint_bin_init_cap(v, len) == BIGINT_OK_E);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = 0xFFFFFFFFU;
    }
    v->len = len;
    v->sign = BIGINT_SIGN_POS_E;
}

static void make_pow2(bigint_bin_ty *v, size_t bits)
{
    const size_t len = bits / 32U + 1U;
    const size_t idx = bits / 32U;
    bigint_bin_free(v);
    CHECK(bigint_bin_init_cap(v, len) == BIGINT_OK_E);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = 0U;
    }
    v->limbs[idx] = (uint32_t)1U << (bits % 32U);
    v->len = idx + 1U;
    v->sign = BIGINT_SIGN_POS_E;
}

static void check_case(const char *name, const bigint_bin_ty *a,
        const bigint_bin_ty *b)
{
    bigint_bin_ty ref;
    bigint_bin_ty par;
    CHECK(bigint_bin_init(&ref) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&par) == BIGINT_OK_E);
    CHECK(bigint_bin_mul(&ref, a, b) == BIGINT_OK_E);

    static const unsigned threads[] = { 1U, 4U, 0U };
    for (size_t t = 0U; t < sizeof(threads) / sizeof(threads[0]); t++) {
        nex_thread_pool_ty *pool = nex_thread_pool_create(threads[t]);
        CHECK(pool != NULL);
        if (pool == NULL) {
            continue;
        }
        CHECK(nex_parallel_bin_mul_ntt(&par, a, b, pool) == BIGINT_OK_E);
        if (bigint_bin_cmp(&par, &ref) != 0) {
            printf("FAIL %s: result mismatch (threads=%u, ref_len=%zu, "
                   "par_len=%zu)\n", name, nex_thread_pool_count(pool),
                   ref.len, par.len);
            g_fail++;
        }
        nex_thread_pool_destroy(pool);
    }

    bigint_bin_free(&par);
    bigint_bin_free(&ref);
}

int main(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    CHECK(bigint_bin_init(&a) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&b) == BIGINT_OK_E);

    /* 8k（核心走 Toom）与 16k/20k（核心走 NTT） */
    make_rand(&a, 8192U);
    make_rand(&b, 8192U);
    check_case("rand 8k x 8k", &a, &b);

    make_rand(&a, 16384U);
    make_rand(&b, 16384U);
    check_case("rand 16k x 16k", &a, &b);

    make_rand(&a, 20000U);
    make_rand(&b, 17000U);
    check_case("rand 20k x 17k", &a, &b);

    make_ones(&a, 16384U);
    make_ones(&b, 16384U);
    check_case("ones 16k x 16k", &a, &b);

    make_pow2(&a, 16U * 16384U);
    make_rand(&b, 16384U);
    check_case("pow2 16k x rand", &a, &b);

    make_rand(&a, 16384U);
    make_rand(&b, 16384U);
    a.sign = BIGINT_SIGN_NEG_E;
    check_case("neg 16k x 16k", &a, &b);
    a.sign = BIGINT_SIGN_NEG_E;
    b.sign = BIGINT_SIGN_NEG_E;
    check_case("neg x neg 16k", &a, &b);

    CHECK(bigint_bin_from_u64(&a, 0U) == BIGINT_OK_E);
    make_rand(&b, 16384U);
    check_case("zero x rand", &a, &b);

    /* 一次 64k 规模（较慢，覆盖 NTT 上限附近的分配/进位） */
    make_rand(&a, 65536U);
    make_rand(&b, 65536U);
    check_case("rand 64k x 64k", &a, &b);

    bigint_bin_free(&b);
    bigint_bin_free(&a);
    if (g_fail == 0) {
        printf("parallel NTT: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
