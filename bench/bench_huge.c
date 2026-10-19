/*
 * bench_huge.c：大整数乘法 P0 可行性基准（issue #98）。
 *
 * 用法：bench_huge <十进制位数> [repeat]
 *
 * 以约 <digits> 位十进制（limbs = ceil(digits·log2(10)/32)）的随机操作数，
 * 强制走核心多模数 CRT NTT，输出单次耗时、Peak RSS、结果肢数。
 * 当前核心 NTT 上限为 sum(limbs) ≤ 2^26、log2n ≤ 27；超出会返回
 * UNSUPPORTED，请先用 scripts/bigint-feasibility.py 估算。
 *
 * 峰值内存：POSIX 用 getrusage(RU_MAXRSS)，Windows 用
 * GetProcessMemoryInfo(PeakWorkingSetSize)。每个进程只应测一个规模。
 */

#if defined(__APPLE__)
/* macOS：ru_maxrss 是 BSD 扩展，_POSIX_C_SOURCE 会把它藏起来 */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#elif !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
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

static uint64_t g_rng = 0x243F6A8885A308D3ULL;

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
    if (bigint_bin_init_cap(v, limbs) != BIGINT_OK_E) {
        fprintf(stderr, "OOM: init_cap(%zu)\n", limbs);
        exit(2);
    }
    for (size_t i = 0U; i < limbs; i++) {
        v->limbs[i] = (uint32_t)rnd64();
    }
    v->limbs[limbs - 1U] |= 0x80000000U;
    v->len = limbs;
    v->sign = BIGINT_SIGN_POS_E;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: %s <十进制位数> [repeat]\n", argv[0]);
        return 2;
    }
    const double digits = strtod(argv[1], NULL);
    const int repeat = (argc > 2) ? atoi(argv[2]) : 1;
    if (!(digits > 0.0) || (repeat <= 0)) {
        fprintf(stderr, "无效参数\n");
        return 2;
    }

    const double bits = digits * log2(10.0);
    const size_t limbs = (size_t)ceil(bits / 32.0);
    if ((limbs == 0U) || (limbs > (size_t)1U << 25U)) {
        fprintf(stderr, "limbs=%zu 超出当前 NTT 上限（等长约 2^25 肢）\n",
                limbs);
        return 2;
    }

    printf("bench_huge: digits≈%.6g bits≈%.6g limbs=%zu repeat=%d\n",
            digits, bits, limbs, repeat);

    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty out;
    (void)bigint_bin_init(&a);
    (void)bigint_bin_init(&b);
    (void)bigint_bin_init(&out);
    make_operand(&a, limbs);
    make_operand(&b, limbs);

    bigint_mul_method_ty method;
    method.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
    method.params.multi_moduli_crt_ntt.mod_count = 0U;

    double best = 1e30;
    bigint_err_ty err = BIGINT_OK_E;
    for (int r = 0; r < repeat; r++) {
        const double t0 = now_ms();
        err = bigint_bin_mul_ex(&out, &a, &b, &method);
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
