/*
 * nex_parallel_bench.c：并行 NTT 乘法基准。
 *
 * 对 16384/32768/65536 肢的操作数，比较：
 *   - 核心 bigint_bin_mul（AUTO，该规模走 NTT，串行）
 *   - nex_parallel_bin_mul_ntt 在 1/2/4/8/16 线程下的耗时
 * 并在每个规模首次校验并行结果与核心结果逐位一致。
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nex_parallel_ntt.h"
#include "nex_thread.h"

#include "nex/bigint/bin/nex_bigint_bin.h"

#include <stdio.h>
#include <stdlib.h>

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

static uint64_t g_rng = 0x243F6A8885A308D3ULL;
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
    (void)bigint_bin_init_cap(v, len);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = (uint32_t)rnd64();
    }
    v->limbs[len - 1U] |= 0x80000000U;
    v->len = len;
    v->sign = BIGINT_SIGN_POS_E;
}

static double time_core(const bigint_bin_ty *a, const bigint_bin_ty *b,
        int reps)
{
    bigint_bin_ty r;
    (void)bigint_bin_init(&r);
    double best = 1e30;
    for (int t = 0; t < reps; t++) {
        const double t0 = now_ms();
        (void)bigint_bin_mul(&r, a, b);
        const double dt = now_ms() - t0;
        if (dt < best) {
            best = dt;
        }
    }
    bigint_bin_free(&r);
    return best;
}

static double time_par(const bigint_bin_ty *a, const bigint_bin_ty *b,
        nex_thread_pool_ty *pool, int reps)
{
    bigint_bin_ty r;
    (void)bigint_bin_init(&r);
    double best = 1e30;
    for (int t = 0; t < reps; t++) {
        const double t0 = now_ms();
        (void)nex_parallel_bin_mul_ntt(&r, a, b, pool);
        const double dt = now_ms() - t0;
        if (dt < best) {
            best = dt;
        }
    }
    bigint_bin_free(&r);
    return best;
}

int main(void)
{
    static const size_t sizes[] = { 16384U, 32768U, 65536U };
    static const unsigned thread_counts[] = { 1U, 2U, 4U, 8U, 16U };

    printf("parallel NTT multiplication benchmark\n");
    printf("%8s %10s %8s %10s %9s %6s\n",
            "limbs", "core_ms", "threads", "par_ms", "speedup", "match");

    for (size_t si = 0U; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        const size_t n = sizes[si];
        bigint_bin_ty a;
        bigint_bin_ty b;
        bigint_bin_ty ref;
        (void)bigint_bin_init(&a);
        (void)bigint_bin_init(&b);
        (void)bigint_bin_init(&ref);
        make_rand(&a, n);
        make_rand(&b, n);

        const int reps = (n <= 16384U) ? 3 : ((n <= 32768U) ? 2 : 1);
        const double core_ms = time_core(&a, &b, reps);
        (void)bigint_bin_mul(&ref, &a, &b);

        for (size_t ti = 0U;
                ti < sizeof(thread_counts) / sizeof(thread_counts[0]); ti++) {
            nex_thread_pool_ty *pool = nex_thread_pool_create(
                    thread_counts[ti]);
            if (pool == NULL) {
                printf("%8zu %10.3f %8u %10s %9s %6s\n", n, core_ms,
                        thread_counts[ti], "-", "-", "pool-fail");
                continue;
            }
            bigint_bin_ty par;
            (void)bigint_bin_init(&par);
            const bigint_err_ty perr = nex_parallel_bin_mul_ntt(&par, &a, &b,
                    pool);
            const int match = (perr == BIGINT_OK_E)
                    && (bigint_bin_cmp(&par, &ref) == 0);
            const double par_ms = time_par(&a, &b, pool, reps);
            printf("%8zu %10.3f %8u %10.3f %9.2fx %6s\n", n, core_ms,
                    thread_counts[ti], par_ms,
                    (par_ms > 0.0) ? (core_ms / par_ms) : 0.0,
                    match ? "ok" : "FAIL");
            bigint_bin_free(&par);
            nex_thread_pool_destroy(pool);
        }

        bigint_bin_free(&ref);
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }
    return 0;
}
