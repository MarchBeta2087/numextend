/*
 * nex_opencl_test.c：contrib OpenCL M1 正确性测试。
 *
 * 覆盖：madd / dot，多个 limbs 与 n，全零、全 1、随机；GPU 结果与
 * CPU 参考逐位一致，并用 bigint_bin 对小组用例交叉验证。
 *
 * 无 OpenCL 平台/设备时默认 SKIP（返回 0）；CI 可设
 * NEX_OPENCL_REQUIRE=1 强制失败。
 */

#include "nex_opencl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;

static uint32_t rnd32(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

static void fill_random(uint32_t *v, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        v[i] = rnd32();
    }
    if (count > 0u) {
        v[count - 1u] |= 0x80000000u;
    }
}

static void fill_ones(uint32_t *v, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        v[i] = 0xFFFFFFFFu;
    }
}

static uint32_t digit_at(const uint32_t *v, size_t limb_index, size_t digit_index)
{
    return (v[limb_index + (digit_index >> 1)] >> ((digit_index & 1u) * 16u))
            & 0xFFFFu;
}

static void pack_digit(uint32_t *v, size_t limb_index, size_t digit_index,
        uint32_t digit)
{
    if ((digit_index & 1u) == 0u) {
        v[limb_index + (digit_index >> 1)] = digit;
    } else {
        v[limb_index + (digit_index >> 1)] |= (digit << 16);
    }
}

static void cpu_carry(const uint64_t *sums, size_t out_digits,
        uint32_t *out, size_t out_limbs)
{
    memset(out, 0, out_limbs * sizeof(uint32_t));
    uint64_t carry = 0;
    for (size_t k = 0; k < out_digits; k++) {
        uint64_t t = sums[k] + carry;
        pack_digit(out, 0u, k, (uint32_t)(t & 0xFFFFu));
        carry = t >> 16;
    }
}

static void cpu_madd(const uint32_t *a, const uint32_t *b, const uint32_t *c,
        uint32_t *out, size_t n, size_t limbs)
{
    const size_t digits = limbs * 2u;
    const size_t out_digits = limbs * 4u + 1u;
    const size_t out_limbs = limbs * 2u + 1u;
    uint64_t *sums = (uint64_t *)calloc(out_digits, sizeof(uint64_t));
    if (sums == NULL) {
        abort();
    }
    for (size_t e = 0; e < n; e++) {
        memset(sums, 0, out_digits * sizeof(uint64_t));
        const uint32_t *ae = a + e * limbs;
        const uint32_t *be = b + e * limbs;
        for (size_t i = 0; i < digits; i++) {
            uint32_t ad = digit_at(ae, 0u, i);
            for (size_t j = 0; j < digits; j++) {
                sums[i + j] += (uint64_t)ad * digit_at(be, 0u, j);
            }
        }
        for (size_t k = 0; k < digits; k++) {
            sums[k] += digit_at(c + e * limbs, 0u, k);
        }
        cpu_carry(sums, out_digits, out + e * out_limbs, out_limbs);
    }
    free(sums);
}

static void cpu_dot(const uint32_t *a, const uint32_t *b, uint32_t *out,
        size_t n, size_t limbs)
{
    const size_t digits = limbs * 2u;
    const size_t out_digits = limbs * 4u + 1u;
    const size_t out_limbs = limbs * 2u + 1u;
    uint64_t *sums = (uint64_t *)calloc(out_digits, sizeof(uint64_t));
    if (sums == NULL) {
        abort();
    }
    for (size_t e = 0; e < n; e++) {
        const uint32_t *ae = a + e * limbs;
        const uint32_t *be = b + e * limbs;
        for (size_t i = 0; i < digits; i++) {
            uint32_t ad = digit_at(ae, 0u, i);
            for (size_t j = 0; j < digits; j++) {
                sums[i + j] += (uint64_t)ad * digit_at(be, 0u, j);
            }
        }
    }
    cpu_carry(sums, out_digits, out, out_limbs);
    free(sums);
}

static int compare_limbs(const uint32_t *x, const uint32_t *y, size_t count,
        const char *tag)
{
    for (size_t i = 0; i < count; i++) {
        if (x[i] != y[i]) {
            printf("FAIL %s at limb %zu: %08x != %08x\n", tag, i, x[i], y[i]);
            g_fail++;
            return -1;
        }
    }
    return 0;
}

static void cross_check_madd(const uint32_t *a, const uint32_t *b,
        const uint32_t *c, const uint32_t *gpu_out, size_t n, size_t limbs)
{
    const size_t out_limbs = limbs * 2u + 1u;
    bigint_bin_ty A;
    bigint_bin_ty B;
    bigint_bin_ty C;
    bigint_bin_ty P;
    bigint_bin_ty S;
    CHECK(bigint_bin_init(&A) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&B) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&C) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&P) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&S) == BIGINT_OK_E);
    for (size_t e = 0; e < n; e++) {
        CHECK(nex_opencl_unpack_bin(a + e * limbs, limbs, &A) == 0);
        CHECK(nex_opencl_unpack_bin(b + e * limbs, limbs, &B) == 0);
        CHECK(nex_opencl_unpack_bin(c + e * limbs, limbs, &C) == 0);
        CHECK(bigint_bin_mul(&P, &A, &B) == BIGINT_OK_E);
        CHECK(bigint_bin_add(&S, &P, &C) == BIGINT_OK_E);
        uint32_t expect[1024];
        CHECK(out_limbs <= 1024u);
        memset(expect, 0, out_limbs * sizeof(uint32_t));
        for (size_t i = 0; i < S.len && i < out_limbs; i++) {
            expect[i] = S.limbs[i];
        }
        compare_limbs(gpu_out + e * out_limbs, expect, out_limbs, "madd/bin");
    }
    bigint_bin_free(&A);
    bigint_bin_free(&B);
    bigint_bin_free(&C);
    bigint_bin_free(&P);
    bigint_bin_free(&S);
}

static void cross_check_dot(const uint32_t *a, const uint32_t *b,
        const uint32_t *gpu_out, size_t n, size_t limbs)
{
    const size_t out_limbs = limbs * 2u + 1u;
    bigint_bin_ty A;
    bigint_bin_ty B;
    bigint_bin_ty P;
    bigint_bin_ty S;
    CHECK(bigint_bin_init(&A) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&B) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&P) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&S) == BIGINT_OK_E);
    CHECK(bigint_bin_from_u64(&S, 0u) == BIGINT_OK_E);
    for (size_t e = 0; e < n; e++) {
        CHECK(nex_opencl_unpack_bin(a + e * limbs, limbs, &A) == 0);
        CHECK(nex_opencl_unpack_bin(b + e * limbs, limbs, &B) == 0);
        CHECK(bigint_bin_mul(&P, &A, &B) == BIGINT_OK_E);
        CHECK(bigint_bin_add(&S, &S, &P) == BIGINT_OK_E);
    }
    uint32_t expect[1024];
    CHECK(out_limbs <= 1024u);
    memset(expect, 0, out_limbs * sizeof(uint32_t));
    for (size_t i = 0; i < S.len && i < out_limbs; i++) {
        expect[i] = S.limbs[i];
    }
    compare_limbs(gpu_out, expect, out_limbs, "dot/bin");
    bigint_bin_free(&A);
    bigint_bin_free(&B);
    bigint_bin_free(&P);
    bigint_bin_free(&S);
}

static void run_case(size_t limbs, size_t n, int ones)
{
    char err[512];
    nex_opencl_ctx_ty *ctx = nex_opencl_create(limbs, err, sizeof(err));
    if (ctx == NULL) {
        printf("FAIL: create(%zu): %s\n", limbs, err);
        g_fail++;
        return;
    }
    const size_t out_limbs = nex_opencl_out_limbs(ctx);
    const size_t a_count = n * limbs;
    uint32_t *a = (uint32_t *)malloc(a_count * sizeof(uint32_t));
    uint32_t *b = (uint32_t *)malloc(a_count * sizeof(uint32_t));
    uint32_t *c = (uint32_t *)malloc(a_count * sizeof(uint32_t));
    uint32_t *gpu = (uint32_t *)malloc(n * out_limbs * sizeof(uint32_t));
    uint32_t *cpu = (uint32_t *)malloc(n * out_limbs * sizeof(uint32_t));
    uint32_t *dot_gpu = (uint32_t *)malloc(out_limbs * sizeof(uint32_t));
    uint32_t *dot_cpu = (uint32_t *)malloc(out_limbs * sizeof(uint32_t));
    CHECK(a != NULL && b != NULL && c != NULL && gpu != NULL && cpu != NULL
            && dot_gpu != NULL && dot_cpu != NULL);
    if (ones) {
        fill_ones(a, a_count);
        fill_ones(b, a_count);
        fill_ones(c, a_count);
    } else {
        fill_random(a, a_count);
        fill_random(b, a_count);
        fill_random(c, a_count);
    }
    CHECK(nex_opencl_madd(ctx, a, b, c, gpu, n) == 0);
    cpu_madd(a, b, c, cpu, n, limbs);
    compare_limbs(gpu, cpu, n * out_limbs, ones ? "madd/ones" : "madd/rand");

    CHECK(nex_opencl_dot(ctx, a, b, dot_gpu, n) == 0);
    cpu_dot(a, b, dot_cpu, n, limbs);
    compare_limbs(dot_gpu, dot_cpu, out_limbs, ones ? "dot/ones" : "dot/rand");

    if (limbs <= 128u) {
        cross_check_madd(a, b, c, gpu, n, limbs);
        cross_check_dot(a, b, dot_gpu, n, limbs);
    }
    free(a);
    free(b);
    free(c);
    free(gpu);
    free(cpu);
    free(dot_gpu);
    free(dot_cpu);
    nex_opencl_destroy(ctx);
}

int main(void)
{
    char err[512];
    nex_opencl_ctx_ty *ctx = nex_opencl_create(104u, err, sizeof(err));
    if (ctx == NULL) {
        printf("SKIP: OpenCL 不可用: %s\n", err);
        return (getenv("NEX_OPENCL_REQUIRE") != NULL) ? 1 : 0;
    }
    printf("device: %s\n", nex_opencl_device_name(ctx));
    printf("limbs: %zu, out_limbs: %zu\n", nex_opencl_limbs(ctx),
            nex_opencl_out_limbs(ctx));

    static const size_t limb_cases[] = { 1u, 2u, 8u, 104u };
    static const size_t n_cases[] = { 1u, 3u, 17u, 257u };
    for (size_t li = 0; li < sizeof(limb_cases) / sizeof(limb_cases[0]); li++) {
        for (size_t ni = 0; ni < sizeof(n_cases) / sizeof(n_cases[0]); ni++) {
            run_case(limb_cases[li], n_cases[ni], 0);
        }
    }
    run_case(8u, 33u, 1);

    nex_opencl_destroy(ctx);
    if (g_fail != 0) {
        printf("FAILED: %d\n", g_fail);
        return 1;
    }
    printf("all nex_opencl tests passed\n");
    return 0;
}
