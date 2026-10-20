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

int main(int argc, char **argv)
{
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
