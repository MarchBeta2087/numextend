/*
 * bench_sweep.c：全面性能扫描基准（按规模扫描 + 算法对比）。
 *
 * 与 bench.c（固定规模冒烟基准）互补：本文件对每类运算做 2^n 规模扫描，
 * 并对乘法强制各方法，用于标定 AUTO 阈值、发现复杂度异常点。
 *
 * 用法：./bench_sweep [quick]
 *   quick：仅跑小规模子集（CI 冒烟）
 */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/nex_bigint_common.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int g_quick = 0;
static uint64_t g_seed = 0x20260814ULL;

static uint64_t rnd64(void) {
    g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_seed;
}

static void rnd_bin(bigint_bin_ty *v, size_t len) {
    bigint_bin_init_cap(v, len > 0U ? len : 1U);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = (uint32_t)rnd64();
    }
    if (len > 0U) {
        v->limbs[len - 1U] |= 0x80000000U;
    }
    v->len = len;
    v->sign = (len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
}

static void rnd_dec(bigint_dec_ty *v, size_t len) {
    bigint_dec_init_cap(v, len > 0U ? len : 1U);
    for (size_t i = 0U; i < len; i++) {
        v->limbs[i] = (uint32_t)(rnd64() % 1000000000ULL);
    }
    if (len > 0U) {
        v->limbs[len - 1U] = (uint32_t)(rnd64() % 900000000ULL) + 100000000U;
    }
    v->len = len;
    v->sign = (len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
}

/* 自适应重复：跑多次取单次最小值；单次预算超过 150ms 即停 */
static double time_op(void (*fn)(void *), void *ctx) {
    int reps = 1;
    double best = 1e30;
    for (;;) {
        const double t0 = now_ms();
        for (int i = 0; i < reps; i++) {
            fn(ctx);
        }
        const double dt = now_ms() - t0;
        const double per = dt / (double)reps;
        if (per < best) {
            best = per;
        }
        if (dt > 150.0 || reps > (1 << 22)) {
            break;
        }
        reps *= 2;
    }
    return best;
}

/* ---------------- 乘法扫描 ---------------- */

typedef struct {
    bigint_bin_ty *dst, *a, *b;
    const bigint_mul_method_ty *m;
} bmul_ctx;
static void run_bmul(void *p) {
    bmul_ctx *c = (bmul_ctx *)p;
    (void)bigint_bin_mul_ex(c->dst, c->a, c->b, c->m);
}

static void sweep_mul(void) {
    static const size_t sizes[] = {64, 128, 256, 512, 1024, 2048, 4096,
                                   8192, 16384, 32768, 65536};
    const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
    printf("== bigint_bin 乘法扫描（单位：ms，n = 单侧肢数）==\n");
    printf("%8s %10s %10s %10s %10s %10s %10s\n",
           "n", "AUTO", "school", "karat", "toom3", "ntt", "fft8");
    for (size_t si = 0U; si < nsizes; si++) {
        const size_t n = sizes[si];
        if (g_quick && n > 8192U) {
            continue;
        }
        bigint_bin_ty a, b, c;
        rnd_bin(&a, n);
        rnd_bin(&b, n);
        bigint_bin_init(&c);
        bmul_ctx ctx = {&c, &a, &b, NULL};

        bigint_mul_method_ty method[6];
        method[0].algo = BIGINT_MUL_AUTO_E; /* 参数被忽略 */
        method[1].algo = BIGINT_MUL_SCHOOLBOOK_E;
        method[1].params.schoolbook.reserved = 0U;
        method[2].algo = BIGINT_MUL_KARATSUBA_E;
        method[2].params.karatsuba.cutoff = 0U;
        method[3].algo = BIGINT_MUL_TOOM_COOK_E;
        method[3].params.toom_cook.k = 3U;
        method[3].params.toom_cook.cutoff = 0U;
        method[4].algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
        method[4].params.multi_moduli_crt_ntt.mod_count = 0U;
        method[5].algo = BIGINT_MUL_FLOAT_COMPLEX_FFT_E;
        method[5].params.float_complex_fft.chunk_bits = 8U;

        const int enabled[6] = {1, (n <= 8192U), (n <= 32768U), 1,
                                (n >= 4096U), (n >= 256U)};
        double best[6];
        for (int i = 0; i < 6; i++) {
            best[i] = 1e30;
        }
        /* 交错多轮（每轮轮转起始方法），取各方法跨轮最小值，抑制热漂移 */
        for (int round = 0; round < 3; round++) {
            for (int i = 0; i < 6; i++) {
                const int idx = (i + round) % 6;
                if (enabled[idx] == 0) {
                    continue;
                }
                ctx.m = &method[idx];
                const double t = time_op(run_bmul, &ctx);
                if (t < best[idx]) {
                    best[idx] = t;
                }
            }
        }

        printf("%8zu %10.4f %10.4f %10.4f %10.4f %10.4f %10.4f\n", n,
               enabled[0] ? best[0] : -1.0, enabled[1] ? best[1] : -1.0,
               enabled[2] ? best[2] : -1.0, enabled[3] ? best[3] : -1.0,
               enabled[4] ? best[4] : -1.0, enabled[5] ? best[5] : -1.0);
        bigint_bin_free(&c);
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }
    printf("\n");
}

/* ---------------- 除法扫描 ---------------- */

typedef struct {
    bigint_bin_ty *q, *r, *a, *b;
} bdiv_ctx;
static void run_bdiv(void *p) {
    bdiv_ctx *c = (bdiv_ctx *)p;
    (void)bigint_bin_div_rem(c->q, c->r, c->a, c->b);
}

static void sweep_div(void) {
    static const size_t sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
    printf("== bigint_bin 除法扫描（被除数 2n 肢 / 除数 n 肢；AUTO 含 BZ）==\n");
    printf("%8s %12s\n", "n", "div_rem");
    for (size_t si = 0U; si < nsizes; si++) {
        const size_t n = sizes[si];
        if (g_quick && n > 1024U) {
            continue;
        }
        bigint_bin_ty a, b, q, r;
        rnd_bin(&a, 2U * n);
        rnd_bin(&b, n);
        bigint_bin_init(&q);
        bigint_bin_init(&r);
        bdiv_ctx ctx = {&q, &r, &a, &b};
        const double t = time_op(run_bdiv, &ctx);
        printf("%8zu %12.4f\n", n, t);
        bigint_bin_free(&r);
        bigint_bin_free(&q);
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }
    printf("\n");
}

/* ---------------- gcd 扫描 ---------------- */

typedef struct {
    bigint_bin_ty *g, *a, *b;
} bgcd_ctx;
static void run_bgcd(void *p) {
    bgcd_ctx *c = (bgcd_ctx *)p;
    (void)bigint_bin_gcd(c->g, c->a, c->b);
}

static void sweep_gcd(void) {
    static const size_t sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
    printf("== bigint_bin gcd 扫描（Lehmer，n 肢随机）==\n");
    printf("%8s %12s\n", "n", "gcd");
    for (size_t si = 0U; si < nsizes; si++) {
        const size_t n = sizes[si];
        if (g_quick && n > 1024U) {
            continue;
        }
        bigint_bin_ty a, b, g;
        rnd_bin(&a, n);
        rnd_bin(&b, n);
        bigint_bin_init(&g);
        bgcd_ctx ctx = {&g, &a, &b};
        const double t = time_op(run_bgcd, &ctx);
        printf("%8zu %12.4f\n", n, t);
        bigint_bin_free(&g);
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }
    printf("\n");
}

/* ---------------- 进制转换 / 字符串 I/O 扫描 ---------------- */

typedef struct {
    bigint_bin_ty *b;
    bigint_dec_ty *d;
    int dir; /* 0 = bin→dec, 1 = dec→bin */
} conv_ctx;
static void run_conv(void *p) {
    conv_ctx *c = (conv_ctx *)p;
    if (c->dir == 0) {
        (void)bigint_conv_bin_to_dec(c->d, c->b);
    } else {
        (void)bigint_conv_dec_to_bin(c->b, c->d);
    }
}

static void sweep_conv(void) {
    static const size_t sizes[] = {256, 512, 1024, 2048, 4096, 8192, 16384};
    const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
    printf("== 进制转换扫描（n = 二进制肢数）==\n");
    printf("%8s %12s %12s\n", "n", "bin->dec", "dec->bin");
    for (size_t si = 0U; si < nsizes; si++) {
        const size_t n = sizes[si];
        if (g_quick && n > 2048U) {
            continue;
        }
        bigint_bin_ty b;
        bigint_dec_ty d;
        rnd_bin(&b, n);
        bigint_dec_init(&d);
        conv_ctx ctx = {&b, &d, 0};
        const double t1 = time_op(run_conv, &ctx);
        ctx.dir = 1;
        const double t2 = time_op(run_conv, &ctx);
        printf("%8zu %12.4f %12.4f\n", n, t1, t2);
        bigint_dec_free(&d);
        bigint_bin_free(&b);
    }
    printf("\n");
}

/* ---------------- 字符串 I/O 扫描 ---------------- */

typedef struct {
    bigint_bin_ty *v;
    char *buf;
    size_t cap;
    int dir; /* 0 = to_str, 1 = from_str */
} str_ctx;
static void run_str(void *p) {
    str_ctx *c = (str_ctx *)p;
    size_t need = 0U;
    if (c->dir == 0) {
        (void)bigint_bin_to_str(c->v, 10U, c->buf, c->cap, &need);
    } else {
        const char *end = NULL;
        (void)bigint_bin_from_str(c->v, c->buf, 10U, &end);
    }
}

static void sweep_str(void) {
    static const size_t sizes[] = {256, 1024, 4096, 16384};
    const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
    printf("== bigint_bin 十进制字符串 I/O 扫描 ==\n");
    printf("%8s %12s %12s\n", "n", "to_str", "from_str");
    for (size_t si = 0U; si < nsizes; si++) {
        const size_t n = sizes[si];
        if (g_quick && n > 1024U) {
            continue;
        }
        bigint_bin_ty v;
        rnd_bin(&v, n);
        size_t need = 0U;
        (void)bigint_bin_to_str(&v, 10U, NULL, 0U, &need);
        char *buf = (char *)malloc(need + 1U);
        (void)bigint_bin_to_str(&v, 10U, buf, need, &need);
        str_ctx ctx = {&v, buf, need, 0};
        const double t1 = time_op(run_str, &ctx);
        ctx.dir = 1;
        const double t2 = time_op(run_str, &ctx);
        printf("%8zu %12.4f %12.4f\n", n, t1, t2);
        free(buf);
        bigint_bin_free(&v);
    }
    printf("\n");
}

/* ---------------- 整数 sqrt（Newton）扫描 ---------------- */

typedef struct {
    bigint_bin_ty *q, *x;
} isqrt_ctx;
static void run_isqrt(void *p) {
    isqrt_ctx *c = (isqrt_ctx *)p;
    (void)bigint_bin_sqrt(c->q, c->x);
}

static void sweep_isqrt(void) {
    static const size_t sizes[] = {256, 512, 1024, 2048, 4096, 8192, 16384};
    const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
    printf("== bigint_bin_sqrt（Newton 整数开方）扫描 ==\n");
    printf("%8s %12s\n", "n", "isqrt");
    for (size_t si = 0U; si < nsizes; si++) {
        const size_t n = sizes[si];
        if (g_quick && n > 1024U) {
            continue;
        }
        bigint_bin_ty x, q;
        rnd_bin(&x, n);
        bigint_bin_init(&q);
        isqrt_ctx ctx = {&q, &x};
        const double t = time_op(run_isqrt, &ctx);
        printf("%8zu %12.4f\n", n, t);
        bigint_bin_free(&q);
        bigint_bin_free(&x);
    }
    printf("\n");
}

/* ---------------- bigfloat / bigdecimal 精度扫描 ---------------- */

typedef struct {
    bigfloat_ty *dst, *a, *b;
    bigfloat_ctx_ty *ctx;
    int op; /* 0 = mul, 1 = div, 2 = sqrt */
} bf_ctx;
static void run_bf(void *p) {
    bf_ctx *c = (bf_ctx *)p;
    if (c->op == 0) {
        (void)bigfloat_mul(c->dst, c->a, c->b, c->ctx);
    } else if (c->op == 1) {
        (void)bigfloat_div(c->dst, c->a, c->b, c->ctx);
    } else {
        (void)bigfloat_sqrt(c->dst, c->a, c->ctx);
    }
}

static void sweep_bigfloat(void) {
    static const uint32_t precs[] = {1024U, 4096U, 8192U, 16384U, 33220U};
    const size_t nprecs = sizeof(precs) / sizeof(precs[0]);
    printf("== bigfloat 精度扫描（p = 尾数位数 bit）==\n");
    printf("%8s %12s %12s %12s\n", "p", "mul", "div", "sqrt");
    for (size_t pi = 0U; pi < nprecs; pi++) {
        const uint32_t p = precs[pi];
        if (g_quick && p > 4096U) {
            continue;
        }
        bigfloat_ctx_ty ctx;
        bigfloat_ctx_make(&ctx, p, 61U, BIGFLOAT_ROUND_NEAREST_EVEN_E);
        bigfloat_ty a, b, c;
        bigfloat_init(&a);
        bigfloat_init(&b);
        bigfloat_init(&c);
        bigint_bin_ty na, nb;
        rnd_bin(&na, p / 32U + 4U);
        rnd_bin(&nb, p / 32U + 4U);
        (void)bigfloat_from_bigint(&a, &na, &ctx);
        (void)bigfloat_from_bigint(&b, &nb, &ctx);
        bf_ctx fc = {&c, &a, &b, &ctx, 0};
        const double tm = time_op(run_bf, &fc);
        fc.op = 1;
        const double td = time_op(run_bf, &fc);
        fc.op = 2;
        const double ts = time_op(run_bf, &fc);
        printf("%8u %12.4f %12.4f %12.4f\n", p, tm, td, ts);
        bigfloat_free(&c);
        bigfloat_free(&b);
        bigfloat_free(&a);
        bigint_bin_free(&nb);
        bigint_bin_free(&na);
    }
    printf("\n");
}

typedef struct {
    bigdecimal_ty *dst, *a, *b;
    bigdecimal_ctx_ty *ctx;
    int op; /* 0 = mul, 1 = sqrt */
} bd_ctx;
static void run_bd(void *p) {
    bd_ctx *c = (bd_ctx *)p;
    if (c->op == 0) {
        (void)bigdecimal_mul(c->dst, c->a, c->b, c->ctx);
    } else {
        (void)bigdecimal_sqrt(c->dst, c->a, c->ctx);
    }
}

static void sweep_bigdecimal(void) {
    static const uint32_t precs[] = {1024U, 4096U, 10000U};
    const size_t nprecs = sizeof(precs) / sizeof(precs[0]);
    printf("== bigdecimal 精度扫描（p = 尾数位数 digit）==\n");
    printf("%8s %12s %12s\n", "p", "mul", "sqrt");
    for (size_t pi = 0U; pi < nprecs; pi++) {
        const uint32_t p = precs[pi];
        if (g_quick && p > 4096U) {
            continue;
        }
        bigdecimal_ctx_ty ctx;
        bigdecimal_ctx_make(&ctx, p, 4U, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
        bigdecimal_ty a, b, c;
        bigdecimal_init(&a);
        bigdecimal_init(&b);
        bigdecimal_init(&c);
        bigint_dec_ty na, nb;
        rnd_dec(&na, p / 9U + 2U);
        rnd_dec(&nb, p / 9U + 2U);
        (void)bigdecimal_from_bigint(&a, &na, &ctx);
        (void)bigdecimal_from_bigint(&b, &nb, &ctx);
        bd_ctx fc = {&c, &a, &b, &ctx, 0};
        const double tm = time_op(run_bd, &fc);
        fc.op = 1;
        const double ts = time_op(run_bd, &fc);
        printf("%8u %12.4f %12.4f\n", p, tm, ts);
        bigdecimal_free(&c);
        bigdecimal_free(&b);
        bigdecimal_free(&a);
        bigint_dec_free(&nb);
        bigint_dec_free(&na);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "quick") == 0) {
        g_quick = 1;
    }
    printf("Numextend 全面性能扫描（%s）\n\n", g_quick ? "quick" : "full");
#if !defined(__OPTIMIZE__)
    /* GCC/Clang 未加 -O 时不定义 __OPTIMIZE__ */
    printf("*** 警告：当前构建未启用优化（-O0），数据不代表发布性能；"
           "请用 -O2/-O3 构建后再对比 ***\n\n");
#endif
    sweep_mul();
    sweep_div();
    sweep_gcd();
    sweep_conv();
    sweep_str();
    sweep_isqrt();
    sweep_bigfloat();
    sweep_bigdecimal();
    printf("扫描完成\n");
    return 0;
}
