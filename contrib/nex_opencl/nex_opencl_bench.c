/*
 * nex_opencl_bench.c：contrib OpenCL M1 基准。
 *
 * 用法：nex_opencl_bench [n] [limbs]
 * 默认 n=10000、limbs=104（约 1000 位十进制）。
 *
 * 输出：CPU 单线程参考、GPU madd/dot（min of 5）、有效传输带宽。
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nex_opencl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static double now_ms(void)
{
    LARGE_INTEGER f;
    LARGE_INTEGER c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif

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

static uint32_t digit_at(const uint32_t *v, size_t digit_index)
{
    return (v[digit_index >> 1] >> ((digit_index & 1u) * 16u)) & 0xFFFFu;
}

static void pack_digit(uint32_t *v, size_t digit_index, uint32_t digit)
{
    if ((digit_index & 1u) == 0u) {
        v[digit_index >> 1] = digit;
    } else {
        v[digit_index >> 1] |= (digit << 16);
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
            uint32_t ad = digit_at(ae, i);
            for (size_t j = 0; j < digits; j++) {
                sums[i + j] += (uint64_t)ad * digit_at(be, j);
            }
        }
        for (size_t k = 0; k < digits; k++) {
            sums[k] += digit_at(c + e * limbs, k);
        }
        memset(out + e * out_limbs, 0, out_limbs * sizeof(uint32_t));
        uint64_t carry = 0;
        for (size_t k = 0; k < out_digits; k++) {
            uint64_t t = sums[k] + carry;
            pack_digit(out + e * out_limbs, k, (uint32_t)(t & 0xFFFFu));
            carry = t >> 16;
        }
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
            uint32_t ad = digit_at(ae, i);
            for (size_t j = 0; j < digits; j++) {
                sums[i + j] += (uint64_t)ad * digit_at(be, j);
            }
        }
    }
    memset(out, 0, out_limbs * sizeof(uint32_t));
    uint64_t carry = 0;
    for (size_t k = 0; k < out_digits; k++) {
        uint64_t t = sums[k] + carry;
        pack_digit(out, k, (uint32_t)(t & 0xFFFFu));
        carry = t >> 16;
    }
    free(sums);
}

static int equal(const uint32_t *x, const uint32_t *y, size_t count)
{
    return memcmp(x, y, count * sizeof(uint32_t)) == 0;
}

static void bigint_dot_ref(const uint32_t *A, size_t lda,
        const uint32_t *B, size_t ldb, size_t row, size_t col,
        size_t k, size_t limbs, uint32_t *out)
{
    bigint_bin_ty av;
    bigint_bin_ty bv;
    bigint_bin_ty pv;
    bigint_bin_ty sv;
    (void)bigint_bin_init(&av);
    (void)bigint_bin_init(&bv);
    (void)bigint_bin_init(&pv);
    (void)bigint_bin_init(&sv);
    (void)bigint_bin_from_u64(&sv, 0u);
    for (size_t p = 0; p < k; p++) {
        (void)nex_opencl_unpack_bin(A + row * lda + p * limbs, limbs, &av);
        (void)nex_opencl_unpack_bin(B + p * ldb + col * limbs, limbs, &bv);
        (void)bigint_bin_mul(&pv, &av, &bv);
        (void)bigint_bin_add(&sv, &sv, &pv);
    }
    const size_t out_limbs = limbs * 2u + 1u;
    memset(out, 0, out_limbs * sizeof(uint32_t));
    for (size_t i = 0; i < sv.len && i < out_limbs; i++) {
        out[i] = sv.limbs[i];
    }
    bigint_bin_free(&av);
    bigint_bin_free(&bv);
    bigint_bin_free(&pv);
    bigint_bin_free(&sv);
}

static int run_matmul_bench(int argc, char **argv)
{
    size_t m = (argc > 2) ? (size_t)strtoull(argv[2], NULL, 10) : 1024u;
    size_t k = (argc > 3) ? (size_t)strtoull(argv[3], NULL, 10) : 1024u;
    size_t n = (argc > 4) ? (size_t)strtoull(argv[4], NULL, 10) : 1024u;
    size_t sel_r = (argc > 5) ? (size_t)strtoull(argv[5], NULL, 10) : 16u;
    size_t sel_c = (argc > 6) ? (size_t)strtoull(argv[6], NULL, 10) : 16u;
    const size_t limbs = 104u;
    if (m == 0u || k == 0u || n == 0u || sel_r == 0u || sel_c == 0u
            || sel_r > m || sel_c > n) {
        fprintf(stderr, "用法: %s matmul [m] [k] [n] [sel_rows] [sel_cols]\n",
                argv[0]);
        return 2;
    }
    char err[512];
    nex_opencl_ctx_ty *ctx = nex_opencl_create(limbs, err, sizeof(err));
    if (ctx == NULL) {
        fprintf(stderr, "OpenCL 不可用: %s\n", err);
        return 1;
    }
    const size_t out_limbs = nex_opencl_out_limbs(ctx);
    const size_t lda = k * limbs;
    const size_t ldb = n * limbs;
    const size_t lda_bytes = m * lda * sizeof(uint32_t);
    const size_t ldb_bytes = k * ldb * sizeof(uint32_t);
    printf("device: %s\n", nex_opencl_device_name(ctx));
    printf("matmul: A=%zux%zu B=%zux%zu C union = %zu rows + %zu cols\n",
            m, k, k, n, sel_r, sel_c);
    printf("memory: A=%.2f GB B=%.2f GB, selected rows/cols = %.2f MB each\n",
            (double)lda_bytes / 1e9, (double)ldb_bytes / 1e9,
            (double)(sel_r * lda * 4u) / 1e6);
    uint32_t *A = (uint32_t *)malloc(lda_bytes);
    uint32_t *B = (uint32_t *)malloc(ldb_bytes);
    if (A == NULL || B == NULL) {
        fprintf(stderr, "OOM: 需要 %.1f GB（A+B）\n",
                (double)(lda_bytes + ldb_bytes) / 1e9);
        free(A);
        free(B);
        nex_opencl_destroy(ctx);
        return 1;
    }
    fill_random(A, m * lda);
    fill_random(B, k * ldb);
    uint32_t *rows = (uint32_t *)malloc(sel_r * sizeof(uint32_t));
    uint32_t *cols = (uint32_t *)malloc(sel_c * sizeof(uint32_t));
    for (size_t i = 0; i < sel_r; i++) {
        rows[i] = (uint32_t)((i * m) / sel_r);
    }
    for (size_t j = 0; j < sel_c; j++) {
        cols[j] = (uint32_t)((j * n) / sel_c);
    }
    const size_t c_rows_limbs = n * out_limbs;
    const size_t c_cols_limbs = sel_c * out_limbs;
    uint32_t *c_rows = (uint32_t *)malloc(sel_r * c_rows_limbs * sizeof(uint32_t));
    uint32_t *c_cols = (uint32_t *)malloc(m * c_cols_limbs * sizeof(uint32_t));
    if (rows == NULL || cols == NULL || c_rows == NULL || c_cols == NULL) {
        fprintf(stderr, "OOM（输出/索引）\n");
        return 1;
    }

    uint32_t *all_cols = (uint32_t *)malloc(n * sizeof(uint32_t));
    for (size_t j = 0; j < n; j++) {
        all_cols[j] = (uint32_t)j;
    }
    double t0 = now_ms();
    int rc = nex_opencl_matmul_subset(ctx, A, lda, B, ldb, rows, sel_r,
            all_cols, n, c_rows, c_rows_limbs, k);
    free(all_cols);
    double rows_ms = now_ms() - t0;
    if (rc != 0) {
        fprintf(stderr, "matmul rows 失败: %s\n", nex_opencl_error(ctx));
        return 1;
    }

    uint32_t *all_rows = (uint32_t *)malloc(m * sizeof(uint32_t));
    for (size_t i = 0; i < m; i++) {
        all_rows[i] = (uint32_t)i;
    }
    t0 = now_ms();
    rc = nex_opencl_matmul_subset(ctx, A, lda, B, ldb, all_rows, m,
            cols, sel_c, c_cols, c_cols_limbs, k);
    double cols_ms = now_ms() - t0;
    free(all_rows);
    if (rc != 0) {
        fprintf(stderr, "matmul cols 失败: %s\n", nex_opencl_error(ctx));
        return 1;
    }

    const double outputs = (double)(sel_r * n + m * sel_c - sel_r * sel_c);
    const double mults = outputs * (double)k;
    printf("GPU rows (%zu x %zu): %.2f s\n", sel_r, n, rows_ms / 1000.0);
    printf("GPU cols (%zu x %zu): %.2f s\n", m, sel_c, cols_ms / 1000.0);
    printf("GPU total: %.2f s  (%.3g big mults, %.3g big-MADD/s)\n",
            (rows_ms + cols_ms) / 1000.0, mults, mults / ((rows_ms + cols_ms) / 1000.0));

    /* 与 bigint 参考抽查 8+8 个输出 */
    uint32_t expect[1024];
    int bad = 0;
    for (size_t t = 0; t < 8u; t++) {
        size_t i = (t * 7u + 1u) % sel_r;
        size_t j = (t * 13u + 3u) % n;
        bigint_dot_ref(A, lda, B, ldb, rows[i], j, k, limbs, expect);
        if (memcmp(expect, c_rows + i * c_rows_limbs + j * out_limbs,
                out_limbs * sizeof(uint32_t)) != 0) {
            bad++;
        }
    }
    for (size_t t = 0; t < 8u; t++) {
        size_t i = (t * 11u + 5u) % m;
        size_t j = (t * 17u + 2u) % sel_c;
        bigint_dot_ref(A, lda, B, ldb, i, cols[j], k, limbs, expect);
        if (memcmp(expect, c_cols + i * c_cols_limbs + j * out_limbs,
                out_limbs * sizeof(uint32_t)) != 0) {
            bad++;
        }
    }
    printf("spot check (16 outputs): %s\n", bad == 0 ? "bit-exact" : "MISMATCH");

    /* CPU 投影：只测少量输出，不跑完整 CPU 基线 */
    double cpu_sample_ms = 0.0;
    const size_t sample = 4u;
    for (size_t t = 0; t < sample; t++) {
        size_t i = (t * 3u) % sel_r;
        size_t j = (t * 5u) % n;
        t0 = now_ms();
        bigint_dot_ref(A, lda, B, ldb, rows[i], j, k, limbs, expect);
        cpu_sample_ms += now_ms() - t0;
    }
    const double cpu_serial_s = (cpu_sample_ms / 1000.0) / (double)sample
            * outputs;
    printf("CPU projection: serial %.1f s (%.2f h), 16-thread ~%.1f s\n",
            cpu_serial_s, cpu_serial_s / 3600.0, cpu_serial_s / 16.0);
    if (cpu_serial_s / 16.0 > 3600.0) {
        printf("CPU 基线跳过：预计 16 线程 > 1 h\n");
    } else {
        printf("CPU 基线在 1 h 内（未在本 bench 中完整执行）\n");
    }

    nex_opencl_destroy(ctx);
    free(A);
    free(B);
    free(rows);
    free(cols);
    free(c_rows);
    free(c_cols);
    return bad == 0 ? 0 : 1;
}


int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "matmul") == 0) {
        return run_matmul_bench(argc, argv);
    }
    size_t n = (argc > 1) ? (size_t)strtoull(argv[1], NULL, 10) : 10000u;
    size_t limbs = (argc > 2) ? (size_t)strtoull(argv[2], NULL, 10) : 104u;
    if (n == 0u || limbs == 0u) {
        fprintf(stderr, "用法: %s [n] [limbs]\n", argv[0]);
        return 2;
    }
    char err[512];
    nex_opencl_ctx_ty *ctx = nex_opencl_create(limbs, err, sizeof(err));
    if (ctx == NULL) {
        fprintf(stderr, "OpenCL 不可用: %s\n", err);
        return 1;
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
    if (a == NULL || b == NULL || c == NULL || gpu == NULL || cpu == NULL
            || dot_gpu == NULL || dot_cpu == NULL) {
        fprintf(stderr, "OOM\n");
        return 1;
    }
    fill_random(a, a_count);
    fill_random(b, a_count);
    fill_random(c, a_count);

    printf("device: %s\n", nex_opencl_device_name(ctx));
    printf("n=%zu limbs=%zu (%zu bit, out_limbs=%zu)\n",
            n, limbs, limbs * 32u, out_limbs);
    printf("data: a/b/c = %.2f MB each, out = %.2f MB\n",
            (double)(a_count * sizeof(uint32_t)) / 1e6,
            (double)(n * out_limbs * sizeof(uint32_t)) / 1e6);

    double t0 = now_ms();
    cpu_madd(a, b, c, cpu, n, limbs);
    double cpu_madd_ms = now_ms() - t0;
    printf("CPU 1-thread madd: %8.2f ms\n", cpu_madd_ms);

    /* warmup + 正确性 */
    if (nex_opencl_madd(ctx, a, b, c, gpu, n) != 0) {
        fprintf(stderr, "madd 失败: %s\n", nex_opencl_error(ctx));
        return 1;
    }
    printf("madd: %s\n", equal(gpu, cpu, n * out_limbs) ? "bit-exact" : "MISMATCH");
    double best = 1e30;
    for (int it = 0; it < 5; it++) {
        double t = now_ms();
        if (nex_opencl_madd(ctx, a, b, c, gpu, n) != 0) {
            fprintf(stderr, "madd 失败: %s\n", nex_opencl_error(ctx));
            return 1;
        }
        double ms = now_ms() - t;
        if (ms < best) {
            best = ms;
        }
    }
    const double madd_bytes = 3.0 * (double)(a_count * sizeof(uint32_t))
            + (double)(n * out_limbs * sizeof(uint32_t));
    printf("GPU madd end-to-end (min/5): %8.2f ms  (%.1f GB/s host traffic)\n",
            best, madd_bytes / (best * 1e6));

    t0 = now_ms();
    cpu_dot(a, b, dot_cpu, n, limbs);
    double cpu_dot_ms = now_ms() - t0;
    printf("CPU 1-thread dot:  %8.2f ms\n", cpu_dot_ms);
    if (nex_opencl_dot(ctx, a, b, dot_gpu, n) != 0) {
        fprintf(stderr, "dot 失败: %s\n", nex_opencl_error(ctx));
        return 1;
    }
    printf("dot:  %s\n", equal(dot_gpu, dot_cpu, out_limbs) ? "bit-exact" : "MISMATCH");
    best = 1e30;
    for (int it = 0; it < 5; it++) {
        double t = now_ms();
        if (nex_opencl_dot(ctx, a, b, dot_gpu, n) != 0) {
            fprintf(stderr, "dot 失败: %s\n", nex_opencl_error(ctx));
            return 1;
        }
        double ms = now_ms() - t;
        if (ms < best) {
            best = ms;
        }
    }
    const double dot_bytes = 2.0 * (double)(a_count * sizeof(uint32_t))
            + (double)(out_limbs * sizeof(uint32_t));
    printf("GPU dot end-to-end (min/5):  %8.2f ms  (%.1f GB/s host traffic)\n",
            best, dot_bytes / (best * 1e6));

    nex_opencl_destroy(ctx);
    free(a);
    free(b);
    free(c);
    free(gpu);
    free(cpu);
    free(dot_gpu);
    free(dot_cpu);
    return 0;
}
