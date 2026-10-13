/*
 * bench.c：性能基准（设计 §13 目标对照）。
 *
 * 目标（§13）：万位十进制整数乘法 < 10ms（schoolbook 可达成）。
 * 测量项：
 *   1. bigint_bin 万位乘法（≈ 1039 肢 → Karatsuba 路径）
 *   2. bigint_bin 万位除法
 *   3. bigint_dec 万位乘法（≈ 1112 肢）
 *   4. bigfrac 万位加法（含二进制 GCD 约分）
 *   5. bigfloat 万位精度乘法 / 除法 / sqrt（mant_bits ≈ 33220）
 *   6. bigdecimal 万位精度乘法 / sqrt（mant_digits = 10000）
 *
 * 计时：Windows 用 QueryPerformanceCounter，其余用 clock()。
 * 用法：./bench [repeats]（默认每项 3 次取最小）。
 */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
static double now_ms(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_ms(void) {
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
}
#endif

static int g_repeats = 3;
static uint64_t g_seed = 0x20260812ULL;
#define ND 10000U  /* 万位十进制 */

static uint64_t rnd64(void) {
    g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_seed;
}

static void rnd_bin(bigint_bin_ty *v, size_t ndigits) {
    bigint_bin_ty ten;
    bigint_bin_init(&ten);
    bigint_bin_from_u64(&ten, 10U);
    bigint_bin_from_u64(v, (uint64_t)(rnd64() % 9U) + 1U);
    for (size_t i = 1U; i < ndigits; i++) {
        bigint_bin_mul(v, v, &ten);
        bigint_bin_ty d;
        bigint_bin_init(&d);
        bigint_bin_from_u64(&d, rnd64() % 10U);
        bigint_bin_add(v, v, &d);
        bigint_bin_free(&d);
    }
    bigint_bin_free(&ten);
}

static void rnd_dec(bigint_dec_ty *v, size_t ndigits) {
    bigint_dec_ty ten;
    bigint_dec_init(&ten);
    bigint_dec_from_u64(&ten, 10U);
    bigint_dec_from_u64(v, (uint64_t)(rnd64() % 9U) + 1U);
    for (size_t i = 1U; i < ndigits; i++) {
        bigint_dec_mul_pow10(v, 1U);
        bigint_dec_ty d;
        bigint_dec_init(&d);
        bigint_dec_from_u64(&d, rnd64() % 10U);
        bigint_dec_add(v, v, &d);
        bigint_dec_free(&d);
    }
    bigint_dec_free(&ten);
}

static double best_time(double *t, int n) {
    double b = t[0];
    for (int i = 1; i < n; i++) {
        if (t[i] < b) {
            b = t[i];
        }
    }
    return b;
}

static void report(const char *name, double *ts) {
    printf("%-42s %8.3f ms\n", name, best_time(ts, g_repeats));
}

static void bench_bin_mul(void) {
    bigint_bin_ty a, b, c;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&c);
    rnd_bin(&a, ND);
    rnd_bin(&b, ND);
    double ts[64];
    for (int r = 0; r < g_repeats; r++) {
        const double t0 = now_ms();
        bigint_bin_mul(&c, &a, &b);
        ts[r] = now_ms() - t0;
    }
    report("bigint_bin 万位乘法", ts);
    bigint_bin_free(&c);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

/* 直接填充随机肢（O(n) 构造；十进制逐位构造为 O(n²)，不适用大尺寸） */
static void rnd_bin_limbs(bigint_bin_ty *v, size_t len) {
    bigint_bin_init_cap(v, len);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = (uint32_t)rnd64();
    }
    if (len > 0U) {
        v->limbs[len - 1U] |= 0x80000000U;
    }
    v->len = len;
    v->sign = BIGINT_SIGN_POS_E;
}

/* NTT 乘法：2^15 肢（≈ 32 万 bit，超过 AUTO 阈值 2^14，触发 NTT） */
/* bin↔dec 分治互转（10 万位十进制 ≈ 10384 bin 肢，超过分治阈值） */
static void bench_bin_conv(void) {
    bigint_bin_ty a;
    bigint_dec_ty d;
    rnd_bin_limbs(&a, 10384U);
    bigint_dec_init(&d);
    double ts[64];
    for (int r = 0; r < g_repeats; r++) {
        const double t0 = now_ms();
        bigint_conv_bin_to_dec(&d, &a);
        ts[r] = now_ms() - t0;
    }
    report("bigint bin→dec 十万位", ts);
    for (int r = 0; r < g_repeats; r++) {
        const double t0 = now_ms();
        bigint_conv_dec_to_bin(&a, &d);
        ts[r] = now_ms() - t0;
    }
    report("bigint dec→bin 十万位", ts);
    bigint_dec_free(&d);
    bigint_bin_free(&a);
}

static void bench_bin_mul_ntt(void) {
    bigint_bin_ty a, b, c;
    bigint_mul_method_ty m;
    rnd_bin_limbs(&a, 1U << 15U);
    rnd_bin_limbs(&b, 1U << 15U);
    bigint_bin_init(&c);
    m.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
    m.params.multi_moduli_crt_ntt.mod_count = 0;
    double ts[64];
    for (int r = 0; r < g_repeats; r++) {
        const double t0 = now_ms();
        bigint_bin_mul_ex(&c, &a, &b, &m);
        ts[r] = now_ms() - t0;
    }
    report("bigint_bin NTT 乘法（32 万 bit）", ts);
    bigint_bin_free(&c);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

static void bench_bin_div(void) {
    bigint_bin_ty a, b, q, r;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&q);
    bigint_bin_init(&r);
    rnd_bin(&a, ND);
    rnd_bin(&b, ND / 2);
    double ts[64];
    for (int rp = 0; rp < g_repeats; rp++) {
        const double t0 = now_ms();
        bigint_bin_div_rem(&q, &r, &a, &b);
        ts[rp] = now_ms() - t0;
    }
    report("bigint_bin 万位除法", ts);
    bigint_bin_free(&r);
    bigint_bin_free(&q);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

static void bench_dec_mul(void) {
    bigint_dec_ty a, b, c;
    bigint_dec_init(&a);
    bigint_dec_init(&b);
    bigint_dec_init(&c);
    rnd_dec(&a, ND);
    rnd_dec(&b, ND);
    double ts[64];
    for (int r = 0; r < g_repeats; r++) {
        const double t0 = now_ms();
        bigint_dec_mul(&c, &a, &b);
        ts[r] = now_ms() - t0;
    }
    report("bigint_dec 万位乘法", ts);
    bigint_dec_free(&c);
    bigint_dec_free(&b);
    bigint_dec_free(&a);
}

static void bench_frac_add(void) {
    bigfrac_ty f1, f2, f3;
    bigint_bin_ty n1, n2;
    bigfrac_init(&f1);
    bigfrac_init(&f2);
    bigfrac_init(&f3);
    bigint_bin_init(&n1);
    bigint_bin_init(&n2);
    rnd_bin(&n1, ND);
    bigint_bin_add(&n2, &n1, &n1);
    bigfrac_from_ints(&f1, &n1, &n2);
    rnd_bin(&n1, ND);
    rnd_bin(&n2, ND);
    bigfrac_from_ints(&f2, &n1, &n2);
    double ts[64];
    for (int r = 0; r < g_repeats; r++) {
        const double t0 = now_ms();
        bigfrac_add(&f3, &f1, &f2);
        ts[r] = now_ms() - t0;
    }
    report("bigfrac 万位加法（含 GCD）", ts);
    bigfrac_free(&f3);
    bigfrac_free(&f2);
    bigfrac_free(&f1);
    bigint_bin_free(&n2);
    bigint_bin_free(&n1);
}

static void bench_bf_ops(void) {
    bigfloat_ctx_ty ctx;
    bigfloat_ctx_make(&ctx, 33220U, 61U, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_ty a, b, c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);
    bigint_bin_ty na, nb;
    bigint_bin_init(&na);
    bigint_bin_init(&nb);
    rnd_bin(&na, ND);
    rnd_bin(&nb, ND);
    bigfloat_from_bigint(&a, &na, &ctx);
    bigfloat_from_bigint(&b, &nb, &ctx);
    {
        double ts[64];
        for (int r = 0; r < g_repeats; r++) {
            const double t0 = now_ms();
            bigfloat_mul(&c, &a, &b, &ctx);
            ts[r] = now_ms() - t0;
        }
        report("bigfloat 万位精度乘法", ts);
    }
    {
        double ts[64];
        for (int r = 0; r < g_repeats; r++) {
            const double t0 = now_ms();
            bigfloat_div(&c, &a, &b, &ctx);
            ts[r] = now_ms() - t0;
        }
        report("bigfloat 万位精度除法", ts);
    }
    {
        double ts[64];
        for (int r = 0; r < g_repeats; r++) {
            const double t0 = now_ms();
            bigfloat_sqrt(&c, &a, &ctx);
            ts[r] = now_ms() - t0;
        }
        report("bigfloat 万位精度 sqrt", ts);
    }
    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
    bigint_bin_free(&nb);
    bigint_bin_free(&na);
}

static void bench_bd_ops(void) {
    bigdecimal_ctx_ty ctx;
    bigdecimal_ctx_make(&ctx, 10000U, 4U, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_ty a, b, c;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&c);
    bigint_dec_ty na, nb;
    bigint_dec_init(&na);
    bigint_dec_init(&nb);
    rnd_dec(&na, ND);
    rnd_dec(&nb, ND);
    bigdecimal_from_bigint(&a, &na, &ctx);
    bigdecimal_from_bigint(&b, &nb, &ctx);
    {
        double ts[64];
        for (int r = 0; r < g_repeats; r++) {
            const double t0 = now_ms();
            bigdecimal_mul(&c, &a, &b, &ctx);
            ts[r] = now_ms() - t0;
        }
        report("bigdecimal 万位精度乘法", ts);
    }
    {
        double ts[64];
        for (int r = 0; r < g_repeats; r++) {
            const double t0 = now_ms();
            bigdecimal_sqrt(&c, &a, &ctx);
            ts[r] = now_ms() - t0;
        }
        report("bigdecimal 万位精度 sqrt", ts);
    }
    bigdecimal_free(&c);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
    bigint_dec_free(&nb);
    bigint_dec_free(&na);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        g_repeats = atoi(argv[1]);
        if (g_repeats < 1) {
            g_repeats = 1;
        }
        if (g_repeats > 64) {
            g_repeats = 64;
        }
    }
    printf("Numextend 性能基准（每项 %d 次取最小；万位 = %zu 位十进制）\n",
            g_repeats, (size_t)ND);
    printf("设计 §13 目标：万位十进制整数乘法 < 10 ms\n\n");

    bench_bin_mul();
    bench_bin_mul_ntt();
    bench_bin_conv();
    bench_bin_div();
    bench_dec_mul();
    bench_frac_add();
    bench_bf_ops();
    bench_bd_ops();

    printf("\n基准完成\n");
    return 0;
}
