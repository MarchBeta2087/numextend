/* nex_block_bench：分块 NTT 乘法 P1 基准（issue #98）。 */

#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#elif !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nex_bigint_block.h"
#include "nex/bigint/bin/nex_bigint_bin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
static double now_ms(void)
{
    LARGE_INTEGER f;
    LARGE_INTEGER c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
static size_t peak_rss_bytes(void)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (size_t)pmc.PeakWorkingSetSize;
    }
    return 0U;
}
#else
#include <sys/resource.h>
#include <time.h>
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
static size_t peak_rss_bytes(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return 0U;
    }
#if defined(__APPLE__)
    return (size_t)ru.ru_maxrss;
#else
    return (size_t)ru.ru_maxrss * 1024U;
#endif
}
#endif

static uint64_t g_rng = 0xA4093822299F31D0ULL;
static uint64_t rnd64(void)
{
    g_rng ^= g_rng << 13U;
    g_rng ^= g_rng >> 7U;
    g_rng ^= g_rng << 17U;
    return g_rng;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: %s <十进制位数> [repeat]\n", argv[0]);
        return 2;
    }
    const double digits = strtod(argv[1], NULL);
    const int repeat = (argc > 2) ? atoi(argv[2]) : 1;
    const size_t limbs = (size_t)ceil(digits * log2(10.0) / 32.0);
    printf("nex_block_bench: digits≈%.6g limbs=%zu repeat=%d\n",
            digits, limbs, repeat);

    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty out;
    (void)bigint_bin_init(&a);
    (void)bigint_bin_init(&b);
    (void)bigint_bin_init(&out);
    if (bigint_bin_init_cap(&a, limbs) != BIGINT_OK_E
            || bigint_bin_init_cap(&b, limbs) != BIGINT_OK_E) {
        fprintf(stderr, "OOM\n");
        return 2;
    }
    for (size_t i = 0U; i < limbs; i++) {
        a.limbs[i] = (uint32_t)rnd64();
        b.limbs[i] = (uint32_t)rnd64();
    }
    a.limbs[limbs - 1U] |= 0x80000000U;
    b.limbs[limbs - 1U] |= 0x80000000U;
    a.len = limbs;
    b.len = limbs;
    a.sign = BIGINT_SIGN_POS_E;
    b.sign = BIGINT_SIGN_POS_E;

    double best = 1e30;
    bigint_err_ty err = BIGINT_OK_E;
    for (int r = 0; r < repeat; r++) {
        const double t0 = now_ms();
        err = nex_block_bin_mul(&out, &a, &b);
        const double dt = now_ms() - t0;
        if (err != BIGINT_OK_E) {
            break;
        }
        if (dt < best) {
            best = dt;
        }
    }
    const size_t peak = peak_rss_bytes();
    printf("time_ms=%.3f peak_rss_mb=%.1f out_limbs=%zu err=%d\n",
            best, (double)peak / (1024.0 * 1024.0), out.len, (int)err);

    bigint_bin_free(&out);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
    return (err == BIGINT_OK_E) ? 0 : 1;
}
