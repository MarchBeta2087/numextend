/*
 * nex_opencl_test.c：contrib OpenCL M1 正确性测试。
 *
 * 覆盖：madd / dot，多个 limbs 与 n，全零、全 1、随机；GPU 结果与
 * CPU 参考逐位一致，并用 bigint_bin 对小组用例交叉验证。
 *
 * 无 OpenCL 平台/设备时默认 SKIP（返回 0）；CI 可设
 * NEX_OPENCL_REQUIRE=1 强制失败。
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

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

static void cpu_matmul_ref(const uint32_t *A, size_t lda,
        const uint32_t *B, size_t ldb,
        const uint32_t *rows, size_t nrows,
        const uint32_t *cols, size_t ncols,
        uint32_t *C, size_t k, size_t limbs)
{
    const size_t out_limbs = limbs * 2u + 1u;
    bigint_bin_ty av;
    bigint_bin_ty bv;
    bigint_bin_ty pv;
    bigint_bin_ty sv;
    CHECK(bigint_bin_init(&av) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&bv) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&pv) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&sv) == BIGINT_OK_E);
    for (size_t i = 0; i < nrows; i++) {
        for (size_t j = 0; j < ncols; j++) {
            CHECK(bigint_bin_from_u64(&sv, 0u) == BIGINT_OK_E);
            for (size_t p = 0; p < k; p++) {
                CHECK(nex_opencl_unpack_bin(
                        A + (size_t)rows[i] * lda + p * limbs, limbs, &av) == 0);
                CHECK(nex_opencl_unpack_bin(
                        B + p * ldb + (size_t)cols[j] * limbs, limbs, &bv) == 0);
                CHECK(bigint_bin_mul(&pv, &av, &bv) == BIGINT_OK_E);
                CHECK(bigint_bin_add(&sv, &sv, &pv) == BIGINT_OK_E);
            }
            uint32_t *dst = C + (i * ncols + j) * out_limbs;
            memset(dst, 0, out_limbs * sizeof(uint32_t));
            CHECK(sv.len <= out_limbs);
            for (size_t q = 0; q < sv.len; q++) {
                dst[q] = sv.limbs[q];
            }
        }
    }
    bigint_bin_free(&av);
    bigint_bin_free(&bv);
    bigint_bin_free(&pv);
    bigint_bin_free(&sv);
}

static void set_tile_env(size_t v)
{
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%zu", v);
#if defined(_WIN32)
    (void)_putenv_s("NEX_OPENCL_MATMUL_TILE", buf);
#else
    (void)setenv("NEX_OPENCL_MATMUL_TILE", buf, 1);
#endif
}

static void unset_tile_env(void)
{
#if defined(_WIN32)
    (void)_putenv_s("NEX_OPENCL_MATMUL_TILE", "");
#else
    (void)unsetenv("NEX_OPENCL_MATMUL_TILE");
#endif
}

static void run_matmul_case(size_t limbs, size_t m, size_t k, size_t n,
        size_t nrows, size_t ncols)
{
    char err[512];
    nex_opencl_ctx_ty *ctx = nex_opencl_create(limbs, err, sizeof(err));
    if (ctx == NULL) {
        printf("FAIL: create(%zu): %s\n", limbs, err);
        g_fail++;
        return;
    }
    const size_t out_limbs = nex_opencl_out_limbs(ctx);
    const size_t lda = k * limbs;
    const size_t ldb = n * limbs;
    const size_t ldc = ncols * out_limbs;
    uint32_t *A = (uint32_t *)malloc(m * lda * sizeof(uint32_t));
    uint32_t *B = (uint32_t *)malloc(k * ldb * sizeof(uint32_t));
    uint32_t *rows = (uint32_t *)malloc(nrows * sizeof(uint32_t));
    uint32_t *cols = (uint32_t *)malloc(ncols * sizeof(uint32_t));
    uint32_t *gpu = (uint32_t *)malloc(nrows * ncols * out_limbs * sizeof(uint32_t));
    uint32_t *cpu = (uint32_t *)malloc(nrows * ncols * out_limbs * sizeof(uint32_t));
    CHECK(A != NULL && B != NULL && rows != NULL && cols != NULL
            && gpu != NULL && cpu != NULL);
    if (A == NULL || B == NULL || rows == NULL || cols == NULL
            || gpu == NULL || cpu == NULL) {
        free(A); free(B); free(rows); free(cols); free(gpu); free(cpu);
        nex_opencl_destroy(ctx);
        return;
    }
    fill_random(A, m * lda);
    fill_random(B, k * ldb);
    for (size_t i = 0; i < nrows; i++) {
        rows[i] = (uint32_t)((i * 7u + 3u) % m);
    }
    for (size_t j = 0; j < ncols; j++) {
        cols[j] = (uint32_t)((j * 5u + 1u) % n);
    }
    if (nex_opencl_matmul_subset(ctx, A, lda, B, ldb, rows, nrows,
            cols, ncols, gpu, ldc, k) != 0) {
        printf("FAIL: matmul_subset: %s\n", nex_opencl_error(ctx));
        g_fail++;
    }
    cpu_matmul_ref(A, lda, B, ldb, rows, nrows, cols, ncols, cpu, k, limbs);
    compare_limbs(gpu, cpu, nrows * ncols * out_limbs, "matmul/subset");
    free(A);
    free(B);
    free(rows);
    free(cols);
    free(gpu);
    free(cpu);
    nex_opencl_destroy(ctx);
}


static int nonzero_limbs(const uint32_t *v, size_t limbs)
{
    for (size_t i = 0; i < limbs; i++) {
        if (v[i] != 0u) {
            return 1;
        }
    }
    return 0;
}

static void fill_sparse_dense(uint32_t *v, size_t count, size_t limbs,
        double density)
{
    for (size_t e = 0; e < count / limbs; e++) {
        if ((double)rnd32() / 4294967296.0 < density) {
            fill_random(v + e * limbs, limbs);
        } else {
            memset(v + e * limbs, 0, limbs * sizeof(uint32_t));
        }
    }
}

static void run_sparse_case(size_t limbs, size_t m, size_t k, size_t n,
        size_t nrows, size_t ncols, double density)
{
    char err[512];
    nex_opencl_ctx_ty *ctx = nex_opencl_create(limbs, err, sizeof(err));
    if (ctx == NULL) {
        printf("FAIL: create(%zu): %s\n", limbs, err);
        g_fail++;
        return;
    }
    const size_t out_limbs = nex_opencl_out_limbs(ctx);
    const size_t lda = k * limbs;
    const size_t ldb = n * limbs;
    const size_t ldc = ncols * out_limbs;
    uint32_t *A = (uint32_t *)calloc(m * lda, sizeof(uint32_t));
    uint32_t *B = (uint32_t *)calloc(k * ldb, sizeof(uint32_t));
    if (A == NULL || B == NULL) {
        abort();
    }
    fill_sparse_dense(A, m * k, limbs, density);
    fill_sparse_dense(B, k * n, limbs, density);

    /* 稠密 → CSR/CSC */
    uint32_t *a_ptr = (uint32_t *)malloc((m + 1u) * sizeof(uint32_t));
    uint32_t *b_ptr = (uint32_t *)malloc((n + 1u) * sizeof(uint32_t));
    size_t annz = 0;
    size_t bnnz = 0;
    a_ptr[0] = 0u;
    for (size_t i = 0; i < m; i++) {
        for (size_t p = 0; p < k; p++) {
            if (nonzero_limbs(A + i * lda + p * limbs, limbs)) {
                annz++;
            }
        }
        a_ptr[i + 1u] = (uint32_t)annz;
    }
    b_ptr[0] = 0u;
    for (size_t j = 0; j < n; j++) {
        for (size_t p = 0; p < k; p++) {
            if (nonzero_limbs(B + p * ldb + j * limbs, limbs)) {
                bnnz++;
            }
        }
        b_ptr[j + 1u] = (uint32_t)bnnz;
    }
    uint32_t *a_idx = (uint32_t *)malloc((annz ? annz : 1u) * sizeof(uint32_t));
    uint32_t *b_idx = (uint32_t *)malloc((bnnz ? bnnz : 1u) * sizeof(uint32_t));
    uint32_t *a_val = (uint32_t *)malloc((annz ? annz : 1u) * limbs * sizeof(uint32_t));
    uint32_t *b_val = (uint32_t *)malloc((bnnz ? bnnz : 1u) * limbs * sizeof(uint32_t));
    size_t ai = 0;
    for (size_t i = 0; i < m; i++) {
        for (size_t p = 0; p < k; p++) {
            if (nonzero_limbs(A + i * lda + p * limbs, limbs)) {
                a_idx[ai] = (uint32_t)p;
                memcpy(a_val + ai * limbs, A + i * lda + p * limbs,
                        limbs * sizeof(uint32_t));
                ai++;
            }
        }
    }
    size_t bi = 0;
    for (size_t j = 0; j < n; j++) {
        for (size_t p = 0; p < k; p++) {
            if (nonzero_limbs(B + p * ldb + j * limbs, limbs)) {
                b_idx[bi] = (uint32_t)p;
                memcpy(b_val + bi * limbs, B + p * ldb + j * limbs,
                        limbs * sizeof(uint32_t));
                bi++;
            }
        }
    }
    nex_opencl_csr_ty csr;
    csr.rows = m;
    csr.nnz = annz;
    csr.row_ptr = a_ptr;
    csr.col_idx = a_idx;
    csr.values = a_val;
    nex_opencl_csc_ty csc;
    csc.cols = n;
    csc.nnz = bnnz;
    csc.col_ptr = b_ptr;
    csc.row_idx = b_idx;
    csc.values = b_val;

    uint32_t *rows = (uint32_t *)malloc(nrows * sizeof(uint32_t));
    uint32_t *cols = (uint32_t *)malloc(ncols * sizeof(uint32_t));
    for (size_t i = 0; i < nrows; i++) rows[i] = (uint32_t)((i * 3u) % m);
    for (size_t j = 0; j < ncols; j++) cols[j] = (uint32_t)((j * 5u + 1u) % n);
    uint32_t *gpu = (uint32_t *)malloc(nrows * ncols * out_limbs * sizeof(uint32_t));
    uint32_t *cpu = (uint32_t *)malloc(nrows * ncols * out_limbs * sizeof(uint32_t));
    if (nex_opencl_matmul_sparse(ctx, &csr, &csc, rows, nrows, cols, ncols,
            gpu, ldc) != 0) {
        printf("FAIL: matmul_sparse: %s\n", nex_opencl_error(ctx));
        g_fail++;
    }
    cpu_matmul_ref(A, lda, B, ldb, rows, nrows, cols, ncols, cpu, k, limbs);
    compare_limbs(gpu, cpu, nrows * ncols * out_limbs, "matmul/sparse");

    free(A);
    free(B);
    free(a_ptr); free(b_ptr); free(a_idx); free(b_idx); free(a_val); free(b_val);
    free(rows); free(cols); free(gpu); free(cpu);
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
    run_matmul_case(1u, 2u, 1u, 2u, 1u, 1u);
    run_matmul_case(8u, 20u, 12u, 18u, 5u, 5u);
    set_tile_env(3u);
    run_matmul_case(4u, 10u, 5u, 9u, 5u, 4u);
    unset_tile_env();
    run_sparse_case(8u, 20u, 12u, 18u, 5u, 4u, 0.3);
    set_tile_env(2u);
    run_sparse_case(6u, 12u, 8u, 10u, 4u, 4u, 0.25);
    unset_tile_env();

    nex_opencl_destroy(ctx);
    if (g_fail != 0) {
        printf("FAILED: %d\n", g_fail);
        return 1;
    }
    printf("all nex_opencl tests passed\n");
    return 0;
}
