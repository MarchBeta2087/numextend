/*
 * bench_math.c：数学函数 / 常数多表达式 / 吞吐量基准（TODO §J10）。
 *
 * 三个可独立选择的部分：
 *   trans      所有公开超越函数在多种精度下的耗时；
 *   const      π / e / ζ(3) / γ 用多种表达式计算到多种精度的耗时；
 *   throughput 单位时间内各规模运算可完成的任务量（ops/s）。
 *
 * 用法：
 *   bench_math [--preset full|quick|ci]
 *              [--sections all|trans,const,throughput]
 *              [--budget SECONDS] [--json FILE]
 *
 * 预设默认预算：full 7200 s（约 2 小时）、quick 600 s、ci 300 s。
 * 达到预算后跳过剩余测量；单次测量可能略超过预算。
 * 常数表达式在首次运行做一次已知数字自检（失败置退出码，不影响基准输出）。
 */
/* clock_gettime 需要 POSIX 特性宏（必须在任何系统头之前） */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"
#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/series/nex_series.h"
#include "nex/transcend/nex_transcend.h"

#include "nex/ball/nex_ball_internal.h"   /* Chudnovsky π（内部） */

#include <math.h>
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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif

typedef enum {
    PRESET_FULL = 0,
    PRESET_QUICK,
    PRESET_CI
} preset_ty;

static preset_ty g_preset = PRESET_FULL;
static double g_budget_s = 7200.0;
static double g_start_ms = 0.0;
static int g_do_trans = 0;
static int g_do_const = 0;
static int g_do_throughput = 0;
static FILE *g_json = NULL;
static int g_fail = 0;

static double elapsed_s(void) {
    return (now_ms() - g_start_ms) / 1000.0;
}

static int budget_hit(void) {
    return (g_budget_s > 0.0) && (elapsed_s() >= g_budget_s);
}

/* 重复测量直到单次 > 150 ms（或 reps 上限），取最优单次耗时 */
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

static void json_measure(const char *section, const char *type,
        const char *op, unsigned precision, double ms) {
    if (g_json != NULL) {
        fprintf(g_json,
                "{\"section\":\"%s\",\"type\":\"%s\",\"op\":\"%s\","
                "\"precision\":%u,\"ms\":%.6f}\n",
                section, type, op, precision, ms);
    }
}

static void json_throughput(const char *op, const char *unit, size_t size,
        double ops_per_s) {
    if (g_json != NULL) {
        fprintf(g_json,
                "{\"section\":\"throughput\",\"op\":\"%s\",\"unit\":\"%s\","
                "\"size\":%zu,\"ops_per_sec\":%.3f}\n",
                op, unit, size, ops_per_s);
    }
}

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static bigfloat_err_ty bf_u64(bigfloat_ty *out, uint64_t v,
        const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty bi;
    bigint_bin_init(&bi);
    bigint_err_ty ierr = bigint_bin_from_u64(&bi, v);
    bigfloat_err_ty err;
    if (ierr == BIGINT_OK_E) {
        err = bigfloat_from_bigint(out, &bi, ctx);
    } else {
        err = (ierr == BIGINT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                         : BIGFLOAT_ERR_INVALID_E;
    }
    bigint_bin_free(&bi);
    return err;
}

static bigdecimal_err_ty bd_u64(bigdecimal_ty *out, uint64_t v,
        const bigdecimal_ctx_ty *ctx) {
    bigint_dec_ty bi;
    bigint_dec_init(&bi);
    bigint_err_ty ierr = bigint_dec_from_u64(&bi, v);
    bigdecimal_err_ty err;
    if (ierr == BIGINT_OK_E) {
        err = bigdecimal_from_bigint(out, &bi, ctx);
    } else {
        err = (ierr == BIGINT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                         : BIGDECIMAL_ERR_INVALID_E;
    }
    bigint_dec_free(&bi);
    return err;
}

/* ------------------------------------------------------------------ */
/* 精度 / 规模表                                                       */
/* ------------------------------------------------------------------ */

static const unsigned g_full_float_precs[] = {64U, 256U, 1024U, 4096U,
                                              16384U, 65536U};
static const unsigned g_quick_float_precs[] = {64U, 256U, 1024U, 4096U};
static const unsigned g_ci_float_precs[] = {64U, 256U, 1024U};
static const unsigned g_full_dec_precs[] = {16U, 50U, 128U, 500U, 2000U,
                                            8000U};
static const unsigned g_quick_dec_precs[] = {16U, 50U, 120U, 500U};
static const unsigned g_ci_dec_precs[] = {16U, 50U, 120U};

static const unsigned *float_precs(size_t *count) {
    if (g_preset == PRESET_CI) {
        *count = sizeof(g_ci_float_precs) / sizeof(g_ci_float_precs[0]);
        return g_ci_float_precs;
    }
    if (g_preset == PRESET_QUICK) {
        *count = sizeof(g_quick_float_precs) / sizeof(g_quick_float_precs[0]);
        return g_quick_float_precs;
    }
    *count = sizeof(g_full_float_precs) / sizeof(g_full_float_precs[0]);
    return g_full_float_precs;
}

static const unsigned *dec_precs(size_t *count) {
    if (g_preset == PRESET_CI) {
        *count = sizeof(g_ci_dec_precs) / sizeof(g_ci_dec_precs[0]);
        return g_ci_dec_precs;
    }
    if (g_preset == PRESET_QUICK) {
        *count = sizeof(g_quick_dec_precs) / sizeof(g_quick_dec_precs[0]);
        return g_quick_dec_precs;
    }
    *count = sizeof(g_full_dec_precs) / sizeof(g_full_dec_precs[0]);
    return g_full_dec_precs;
}

static const size_t g_full_limbs[] = {32U, 128U, 512U, 2048U, 8192U};
static const size_t g_quick_limbs[] = {32U, 128U, 512U};
static const size_t g_ci_limbs[] = {32U, 128U};
static const unsigned g_full_bits[] = {256U, 1024U, 4096U, 16384U, 32768U};
static const unsigned g_quick_bits[] = {256U, 1024U};
static const unsigned g_ci_bits[] = {256U, 1024U};
static const unsigned g_full_digits[] = {50U, 200U, 1000U};
static const unsigned g_quick_digits[] = {50U, 200U};
static const unsigned g_ci_digits[] = {50U, 200U};

static const size_t *through_limbs(size_t *count) {
    if (g_preset == PRESET_CI) {
        *count = sizeof(g_ci_limbs) / sizeof(g_ci_limbs[0]);
        return g_ci_limbs;
    }
    if (g_preset == PRESET_QUICK) {
        *count = sizeof(g_quick_limbs) / sizeof(g_quick_limbs[0]);
        return g_quick_limbs;
    }
    *count = sizeof(g_full_limbs) / sizeof(g_full_limbs[0]);
    return g_full_limbs;
}

static const unsigned *through_bits(size_t *count) {
    if (g_preset == PRESET_CI) {
        *count = sizeof(g_ci_bits) / sizeof(g_ci_bits[0]);
        return g_ci_bits;
    }
    if (g_preset == PRESET_QUICK) {
        *count = sizeof(g_quick_bits) / sizeof(g_quick_bits[0]);
        return g_quick_bits;
    }
    *count = sizeof(g_full_bits) / sizeof(g_full_bits[0]);
    return g_full_bits;
}

static const unsigned *through_digits(size_t *count) {
    if (g_preset == PRESET_CI) {
        *count = sizeof(g_ci_digits) / sizeof(g_ci_digits[0]);
        return g_ci_digits;
    }
    if (g_preset == PRESET_QUICK) {
        *count = sizeof(g_quick_digits) / sizeof(g_quick_digits[0]);
        return g_quick_digits;
    }
    *count = sizeof(g_full_digits) / sizeof(g_full_digits[0]);
    return g_full_digits;
}

/* ------------------------------------------------------------------ */
/* 1) 超越函数                                                         */
/* ------------------------------------------------------------------ */

typedef bigfloat_err_ty (*bf_unary_fn)(bigfloat_ty *, const bigfloat_ty *,
        const bigfloat_ctx_ty *);
typedef bigdecimal_err_ty (*bd_unary_fn)(bigdecimal_ty *,
        const bigdecimal_ty *, const bigdecimal_ctx_ty *);
typedef bigcomplex_float_err_ty (*bcf_unary_fn)(bigcomplex_float_ty *,
        const bigcomplex_float_ty *, const bigfloat_ctx_ty *);
typedef bigcomplex_decimal_err_ty (*bcd_unary_fn)(bigcomplex_decimal_ty *,
        const bigcomplex_decimal_ty *, const bigdecimal_ctx_ty *);

typedef struct {
    bf_unary_fn fn;
    bigfloat_ty *dst;
    const bigfloat_ty *src;
    const bigfloat_ctx_ty *ctx;
} bf_job;

static void run_bf(void *p) {
    bf_job *j = (bf_job *)p;
    (void)j->fn(j->dst, j->src, j->ctx);
}

typedef struct {
    bd_unary_fn fn;
    bigdecimal_ty *dst;
    const bigdecimal_ty *src;
    const bigdecimal_ctx_ty *ctx;
} bd_job;

static void run_bd(void *p) {
    bd_job *j = (bd_job *)p;
    (void)j->fn(j->dst, j->src, j->ctx);
}

typedef struct {
    bcf_unary_fn fn;
    bigcomplex_float_ty *dst;
    const bigcomplex_float_ty *src;
    const bigfloat_ctx_ty *ctx;
} bcf_job;

static void run_bcf(void *p) {
    bcf_job *j = (bcf_job *)p;
    (void)j->fn(j->dst, j->src, j->ctx);
}

typedef struct {
    bcd_unary_fn fn;
    bigcomplex_decimal_ty *dst;
    const bigcomplex_decimal_ty *src;
    const bigdecimal_ctx_ty *ctx;
} bcd_job;

static void run_bcd(void *p) {
    bcd_job *j = (bcd_job *)p;
    (void)j->fn(j->dst, j->src, j->ctx);
}

typedef struct {
    bigfloat_ty *dst;
    const bigcomplex_float_ty *src;
    const bigfloat_ctx_ty *ctx;
} bcf_arg_job;

static void run_bcf_arg(void *p) {
    bcf_arg_job *j = (bcf_arg_job *)p;
    (void)bigcomplex_float_arg(j->dst, j->src, j->ctx);
}

typedef struct {
    bigdecimal_ty *dst;
    const bigcomplex_decimal_ty *src;
    const bigdecimal_ctx_ty *ctx;
} bcd_arg_job;

static void run_bcd_arg(void *p) {
    bcd_arg_job *j = (bcd_arg_job *)p;
    (void)bigcomplex_decimal_arg(j->dst, j->src, j->ctx);
}

typedef struct {
    bigfloat_ty *dst;
    const bigfloat_ty *y;
    const bigfloat_ty *x;
    const bigfloat_ctx_ty *ctx;
} bf2_job;

static void run_bf_atan2(void *p) {
    bf2_job *j = (bf2_job *)p;
    (void)bigfloat_atan2(j->dst, j->y, j->x, j->ctx);
}

typedef struct {
    bigdecimal_ty *dst;
    const bigdecimal_ty *y;
    const bigdecimal_ty *x;
    const bigdecimal_ctx_ty *ctx;
} bd2_job;

static void run_bd_atan2(void *p) {
    bd2_job *j = (bd2_job *)p;
    (void)bigdecimal_atan2(j->dst, j->y, j->x, j->ctx);
}

static void trans_float(unsigned p) {
    bigfloat_ctx_ty ctx;
    if (bigfloat_ctx_make(&ctx, p, 61U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E) {
        return;
    }
    bigfloat_ty x;
    bigfloat_ty x2;
    bigfloat_ty y;
    bigfloat_ty dst;
    bigfloat_init(&x);
    bigfloat_init(&x2);
    bigfloat_init(&y);
    bigfloat_init(&dst);
    (void)bigfloat_from_str(&x, "1.234567890123456789", &ctx, NULL);
    (void)bigfloat_from_str(&x2, "0.3", &ctx, NULL);
    (void)bigfloat_from_str(&y, "0.7", &ctx, NULL);
    static const struct {
        const char *name;
        bf_unary_fn fn;
        int which;
    } ops[] = {
        {"exp", bigfloat_exp, 0}, {"log", bigfloat_log, 0},
        {"sin", bigfloat_sin, 0}, {"cos", bigfloat_cos, 0},
        {"tan", bigfloat_tan, 0}, {"asin", bigfloat_asin, 1},
        {"acos", bigfloat_acos, 1}, {"atan", bigfloat_atan, 0},
        {"sqrt", bigfloat_sqrt, 0},
    };
    printf("trans bigfloat p=%u\n", p);
    for (size_t i = 0U; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (budget_hit()) {
            printf("  (budget reached)\n");
            break;
        }
        bf_job job = {ops[i].fn, &dst,
                (ops[i].which == 0) ? &x : &x2, &ctx};
        const double ms = time_op(run_bf, &job);
        printf("  %-8s %12.4f ms\n", ops[i].name, ms);
        json_measure("trans", "bigfloat", ops[i].name, p, ms);
    }
    if (!budget_hit()) {
        bf2_job job = {&dst, &y, &x, &ctx};
        const double ms = time_op(run_bf_atan2, &job);
        printf("  %-8s %12.4f ms\n", "atan2", ms);
        json_measure("trans", "bigfloat", "atan2", p, ms);
    }
    bigfloat_free(&dst);
    bigfloat_free(&y);
    bigfloat_free(&x2);
    bigfloat_free(&x);
}

static void trans_decimal(unsigned d) {
    bigdecimal_ctx_ty ctx;
    if (bigdecimal_ctx_make(&ctx, d, 12U,
            BIGDECIMAL_ROUND_NEAREST_EVEN_E) != BIGDECIMAL_OK_E) {
        return;
    }
    bigdecimal_ty x;
    bigdecimal_ty x2;
    bigdecimal_ty y;
    bigdecimal_ty dst;
    bigdecimal_init(&x);
    bigdecimal_init(&x2);
    bigdecimal_init(&y);
    bigdecimal_init(&dst);
    (void)bigdecimal_from_str(&x, "1.234567890123456789", &ctx, NULL);
    (void)bigdecimal_from_str(&x2, "0.3", &ctx, NULL);
    (void)bigdecimal_from_str(&y, "0.7", &ctx, NULL);
    static const struct {
        const char *name;
        bd_unary_fn fn;
        int which;
    } ops[] = {
        {"exp", bigdecimal_exp, 0}, {"log", bigdecimal_log, 0},
        {"sin", bigdecimal_sin, 0}, {"cos", bigdecimal_cos, 0},
        {"tan", bigdecimal_tan, 0}, {"asin", bigdecimal_asin, 1},
        {"acos", bigdecimal_acos, 1}, {"atan", bigdecimal_atan, 0},
        {"sqrt", bigdecimal_sqrt, 0},
    };
    printf("trans bigdecimal p=%u\n", d);
    for (size_t i = 0U; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (budget_hit()) {
            printf("  (budget reached)\n");
            break;
        }
        bd_job job = {ops[i].fn, &dst,
                (ops[i].which == 0) ? &x : &x2, &ctx};
        const double ms = time_op(run_bd, &job);
        printf("  %-8s %12.4f ms\n", ops[i].name, ms);
        json_measure("trans", "bigdecimal", ops[i].name, d, ms);
    }
    if (!budget_hit()) {
        bd2_job job = {&dst, &y, &x, &ctx};
        const double ms = time_op(run_bd_atan2, &job);
        printf("  %-8s %12.4f ms\n", "atan2", ms);
        json_measure("trans", "bigdecimal", "atan2", d, ms);
    }
    bigdecimal_free(&dst);
    bigdecimal_free(&y);
    bigdecimal_free(&x2);
    bigdecimal_free(&x);
}

static void trans_cfloat(unsigned p) {
    bigfloat_ctx_ty ctx;
    if (bigfloat_ctx_make(&ctx, p, 61U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E) {
        return;
    }
    bigcomplex_float_ty z;
    bigcomplex_float_ty dst;
    bigcomplex_float_init(&z);
    bigcomplex_float_init(&dst);
    (void)bigcomplex_float_from_str(&z, "0.75+1.25i", &ctx, NULL);
    static const struct {
        const char *name;
        bcf_unary_fn fn;
    } ops[] = {
        {"exp", bigcomplex_float_exp}, {"log", bigcomplex_float_log},
        {"sqrt", bigcomplex_float_sqrt}, {"sin", bigcomplex_float_sin},
        {"cos", bigcomplex_float_cos}, {"tan", bigcomplex_float_tan},
    };
    printf("trans complex<float> p=%u\n", p);
    for (size_t i = 0U; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (budget_hit()) {
            printf("  (budget reached)\n");
            break;
        }
        bcf_job job = {ops[i].fn, &dst, &z, &ctx};
        const double ms = time_op(run_bcf, &job);
        printf("  %-8s %12.4f ms\n", ops[i].name, ms);
        json_measure("trans", "bigcomplex_float", ops[i].name, p, ms);
    }
    if (!budget_hit()) {
        bcf_arg_job job = {&dst.re, &z, &ctx};
        const double ms = time_op(run_bcf_arg, &job);
        printf("  %-8s %12.4f ms\n"
, "arg", ms);
        json_measure("trans", "bigcomplex_float", "arg", p, ms);
    }
    bigcomplex_float_free(&dst);
    bigcomplex_float_free(&z);
}

static void trans_cdecimal(unsigned d) {
    bigdecimal_ctx_ty ctx;
    if (bigdecimal_ctx_make(&ctx, d, 12U,
            BIGDECIMAL_ROUND_NEAREST_EVEN_E) != BIGDECIMAL_OK_E) {
        return;
    }
    bigcomplex_decimal_ty z;
    bigcomplex_decimal_ty dst;
    bigcomplex_decimal_init(&z);
    bigcomplex_decimal_init(&dst);
    (void)bigcomplex_decimal_from_str(&z, "0.75+1.25i", &ctx, NULL);
    static const struct {
        const char *name;
        bcd_unary_fn fn;
    } ops[] = {
        {"exp", bigcomplex_decimal_exp}, {"log", bigcomplex_decimal_log},
        {"sqrt", bigcomplex_decimal_sqrt}, {"sin", bigcomplex_decimal_sin},
        {"cos", bigcomplex_decimal_cos}, {"tan", bigcomplex_decimal_tan},
    };
    printf("trans complex<decimal> p=%u\n", d);
    for (size_t i = 0U; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (budget_hit()) {
            printf("  (budget reached)\n");
            break;
        }
        bcd_job job = {ops[i].fn, &dst, &z, &ctx};
        const double ms = time_op(run_bcd, &job);
        printf("  %-8s %12.4f ms\n", ops[i].name, ms);
        json_measure("trans", "bigcomplex_decimal", ops[i].name, d, ms);
    }
    if (!budget_hit()) {
        bcd_arg_job job = {&dst.re, &z, &ctx};
        const double ms = time_op(run_bcd_arg, &job);
        printf("  %-8s %12.4f ms\n"
, "arg", ms);
        json_measure("trans", "bigcomplex_decimal", "arg", d, ms);
    }
    bigcomplex_decimal_free(&dst);
    bigcomplex_decimal_free(&z);
}

static void sweep_trans(void) {
    size_t nf = 0U;
    size_t nd = 0U;
    const unsigned *fp = float_precs(&nf);
    const unsigned *dp = dec_precs(&nd);
    printf("\n== [trans] 超越函数精度扫描 ==\n");
    for (size_t i = 0U; i < nf; i++) {
        if (budget_hit()) {
            printf("(budget reached)\n");
            return;
        }
        trans_float(fp[i]);
        trans_cfloat(fp[i]);
    }
    for (size_t i = 0U; i < nd; i++) {
        if (budget_hit()) {
            printf("(budget reached)\n");
            return;
        }
        trans_decimal(dp[i]);
        trans_cdecimal(dp[i]);
    }
}

/* ------------------------------------------------------------------ */
/* 2) 常数：多表达式                                                   */
/* ------------------------------------------------------------------ */

/* ζ(3) = (5/2) Σ (−1)^{n−1}/(n³ C(2n,n)) */
static bigfloat_err_ty zeta3_apery_f(bigfloat_ty *out,
        const bigfloat_ctx_ty *ctx, size_t nterms) {
    bigfloat_ty sum;
    bigfloat_ty term;
    bigfloat_ty f;
    bigfloat_init(&sum);
    bigfloat_init(&term);
    bigfloat_init(&f);
    bigfloat_err_ty err = bigfloat_from_f64(&term, 0.5);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(&sum, &term);
    }
    for (size_t n = 1U; n + 1U < nterms && err == BIGFLOAT_OK_E; n++) {
        const uint64_t num = (uint64_t)n * n * n;
        const uint64_t den = 2U * (uint64_t)(n + 1U) * (uint64_t)(n + 1U)
                * (2U * (uint64_t)n + 1U);
        err = bf_u64(&f, num, ctx);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_mul(&term, &term, &f, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bf_u64(&f, den, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_div(&term, &term, &f, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_neg(&term);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&sum, &sum, &term, ctx);
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&f, 2.5);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(out, &sum, &f, ctx);
    }
    bigfloat_free(&f);
    bigfloat_free(&term);
    bigfloat_free(&sum);
    return err;
}

static bigdecimal_err_ty zeta3_apery_d(bigdecimal_ty *out,
        const bigdecimal_ctx_ty *ctx, size_t nterms) {
    bigdecimal_ty sum;
    bigdecimal_ty term;
    bigdecimal_ty f;
    bigdecimal_init(&sum);
    bigdecimal_init(&term);
    bigdecimal_init(&f);
    bigdecimal_err_ty err = bigdecimal_from_str(&term, "0.5", ctx, NULL);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&sum, &term);
    }
    for (size_t n = 1U; n + 1U < nterms && err == BIGDECIMAL_OK_E; n++) {
        const uint64_t num = (uint64_t)n * n * n;
        const uint64_t den = 2U * (uint64_t)(n + 1U) * (uint64_t)(n + 1U)
                * (2U * (uint64_t)n + 1U);
        err = bd_u64(&f, num, ctx);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_mul(&term, &term, &f, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_u64(&f, den, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_div(&term, &term, &f, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_neg(&term);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&sum, &sum, &term, ctx);
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&f, "2.5", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_mul(out, &sum, &f, ctx);
    }
    bigdecimal_free(&f);
    bigdecimal_free(&term);
    bigdecimal_free(&sum);
    return err;
}

/* ζ(3) = (4/3)·η(3)，η(3) 用 Euler 变换（O(M²)，仅低精度启用） */
static bigfloat_err_ty zeta3_euler_f(bigfloat_ty *out,
        const bigfloat_ctx_ty *ctx, size_t m) {
    bigfloat_ty *d = (bigfloat_ty *)malloc((m + 1U) * sizeof(bigfloat_ty));
    if (d == NULL) {
        return BIGFLOAT_ERR_OOM_E;
    }
    for (size_t i = 0U; i <= m; i++) {
        bigfloat_init(&d[i]);
    }
    bigfloat_ty f;
    bigfloat_ty sum;
    bigfloat_ty pow2;
    bigfloat_init(&f);
    bigfloat_init(&sum);
    bigfloat_init(&pow2);
    bigfloat_err_ty err = bigfloat_from_f64(&sum, 0.0);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&pow2, 2.0);
    }
    for (size_t j = 0U; j <= m && err == BIGFLOAT_OK_E; j++) {
        const uint64_t k = (uint64_t)j + 1U;
        err = bf_u64(&f, k * k * k, ctx);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_from_f64(&d[j], 1.0);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_div(&d[j], &d[j], &f, ctx);
        }
    }
    for (size_t k = 0U; k <= m && err == BIGFLOAT_OK_E; k++) {
        err = bigfloat_div(&f, &d[0], &pow2, ctx);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&sum, &sum, &f, ctx);
        }
        for (size_t j = 0U; j + k < m && err == BIGFLOAT_OK_E; j++) {
            err = bigfloat_sub(&d[j], &d[j], &d[j + 1U], ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&pow2, &pow2, &pow2, ctx);
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&f, 4.0 / 3.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(out, &sum, &f, ctx);
    }
    bigfloat_free(&pow2);
    bigfloat_free(&sum);
    bigfloat_free(&f);
    for (size_t i = 0U; i <= m; i++) {
        bigfloat_free(&d[i]);
    }
    free(d);
    return err;
}

/* γ = S0(2n)/I0(2n) − ln n（Brent–McMillan；误差 O(e^{−4n})） */
static bigfloat_err_ty gamma_bm_f(bigfloat_ty *out,
        const bigfloat_ctx_ty *ctx, size_t n) {
    bigfloat_ty term;
    bigfloat_ty I;
    bigfloat_ty S;
    bigfloat_ty H;
    bigfloat_ty f;
    bigfloat_ty nf;
    bigfloat_ty n2;
    bigfloat_init(&term);
    bigfloat_init(&I);
    bigfloat_init(&S);
    bigfloat_init(&H);
    bigfloat_init(&f);
    bigfloat_init(&nf);
    bigfloat_init(&n2);
    bigfloat_err_ty err = bigfloat_from_f64(&term, 1.0);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&I, 1.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&S, 0.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&H, 0.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bf_u64(&nf, (uint64_t)n, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bf_u64(&n2, (uint64_t)n * (uint64_t)n, ctx);
    }
    bigfloat_ty one;
    bigfloat_init(&one);
    (void)bigfloat_from_f64(&one, 1.0);
    const size_t kmax = 4U * n + 64U;
    for (size_t k = 1U; k <= kmax && err == BIGFLOAT_OK_E; k++) {
        err = bigfloat_mul(&term, &term, &n2, ctx);
        if (err == BIGFLOAT_OK_E) {
            err = bf_u64(&f, (uint64_t)k * (uint64_t)k, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_div(&term, &term, &f, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bf_u64(&f, (uint64_t)k, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_div(&f, &one, &f, ctx);   /* 1/k */
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&H, &H, &f, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&I, &I, &term, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_mul(&f, &term, &H, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&S, &S, &f, ctx);
        }
    }
    bigfloat_free(&one);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_div(&S, &S, &I, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_log(&f, &nf, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(out, &S, &f, ctx);
    }
    bigfloat_free(&n2);
    bigfloat_free(&nf);
    bigfloat_free(&f);
    bigfloat_free(&H);
    bigfloat_free(&S);
    bigfloat_free(&I);
    bigfloat_free(&term);
    return err;
}

static bigdecimal_err_ty gamma_bm_d(bigdecimal_ty *out,
        const bigdecimal_ctx_ty *ctx, size_t n) {
    bigdecimal_ty term;
    bigdecimal_ty I;
    bigdecimal_ty S;
    bigdecimal_ty H;
    bigdecimal_ty f;
    bigdecimal_ty nf;
    bigdecimal_ty n2;
    bigdecimal_init(&term);
    bigdecimal_init(&I);
    bigdecimal_init(&S);
    bigdecimal_init(&H);
    bigdecimal_init(&f);
    bigdecimal_init(&nf);
    bigdecimal_init(&n2);
    bigdecimal_err_ty err = bigdecimal_from_str(&term, "1", ctx, NULL);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&I, "1", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&S, "0", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&H, "0", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bd_u64(&nf, (uint64_t)n, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bd_u64(&n2, (uint64_t)n * (uint64_t)n, ctx);
    }
    bigdecimal_ty one;
    bigdecimal_init(&one);
    (void)bigdecimal_from_str(&one, "1", ctx, NULL);
    const size_t kmax = 4U * n + 64U;
    for (size_t k = 1U; k <= kmax && err == BIGDECIMAL_OK_E; k++) {
        err = bigdecimal_mul(&term, &term, &n2, ctx);
        if (err == BIGDECIMAL_OK_E) {
            err = bd_u64(&f, (uint64_t)k * (uint64_t)k, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_div(&term, &term, &f, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_u64(&f, (uint64_t)k, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_div(&f, &one, &f, ctx);   /* 1/k */
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&H, &H, &f, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&I, &I, &term, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_mul(&f, &term, &H, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&S, &S, &f, ctx);
        }
    }
    bigdecimal_free(&one);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_div(&S, &S, &I, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_log(&f, &nf, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(out, &S, &f, ctx);
    }
    bigdecimal_free(&n2);
    bigdecimal_free(&nf);
    bigdecimal_free(&f);
    bigdecimal_free(&H);
    bigdecimal_free(&S);
    bigdecimal_free(&I);
    bigdecimal_free(&term);
    return err;
}

/* 常数表达式作业（float） */
typedef struct {
    const bigfloat_ctx_ty *ctx;
    bigfloat_ty one;
    bigfloat_ty v1;
    bigfloat_ty v2;
    bigfloat_ty out;
    bigfrac_ty eps;
    int op;
    size_t n;
} cjob_f;

/* op: 0 4atan1, 1 2asin1, 2 acos-1, 3 2acos0, 4 Machin,
       10 exp1, 11 sqrt(exp2), 12 exp(.5)^2, 13 series exp(1),
       20 apery ζ3, 21 Euler η3, 30 B-M γ */
static void run_cjob_f(void *p) {
    cjob_f *j = (cjob_f *)p;
    bigfloat_ty t;
    bigfloat_init(&t);
    switch (j->op) {
        case 0:
            (void)bigfloat_atan(&t, &j->one, j->ctx);
            (void)bigfloat_add(&t, &t, &t, j->ctx);
            (void)bigfloat_add(&j->out, &t, &t, j->ctx);
            break;
        case 1:
            (void)bigfloat_asin(&t, &j->one, j->ctx);
            (void)bigfloat_add(&j->out, &t, &t, j->ctx);
            break;
        case 2:
            (void)bigfloat_acos(&j->out, &j->v1, j->ctx);
            break;
        case 3:
            (void)bigfloat_acos(&t, &j->v2, j->ctx);
            (void)bigfloat_add(&j->out, &t, &t, j->ctx);
            break;
        case 4: {
            bigfloat_ty u;
            bigfloat_init(&u);
            (void)bigfloat_atan(&t, &j->v1, j->ctx);   /* atan(1/5) */
            (void)bigfloat_add(&t, &t, &t, j->ctx);
            (void)bigfloat_add(&t, &t, &t, j->ctx);
            (void)bigfloat_add(&t, &t, &t, j->ctx);
            (void)bigfloat_add(&t, &t, &t, j->ctx);    /* ×16 */
            (void)bigfloat_atan(&u, &j->v2, j->ctx);   /* atan(1/239) */
            (void)bigfloat_add(&u, &u, &u, j->ctx);
            (void)bigfloat_add(&u, &u, &u, j->ctx);    /* ×4 */
            (void)bigfloat_sub(&j->out, &t, &u, j->ctx);
            bigfloat_free(&u);
            break;
        }
        case 10:
            (void)bigfloat_exp(&j->out, &j->one, j->ctx);
            break;
        case 11:
            (void)bigfloat_exp(&t, &j->v1, j->ctx);   /* exp(2) */
            (void)bigfloat_sqrt(&j->out, &t, j->ctx);
            break;
        case 12:
            (void)bigfloat_exp(&t, &j->v2, j->ctx);   /* exp(0.5) */
            (void)bigfloat_mul(&j->out, &t, &t, j->ctx);
            break;
        case 13:
            (void)nex_series_exp_eval_float(&j->out, NULL, &j->one,
                    &j->eps, j->ctx);
            break;
        case 20:
            (void)zeta3_apery_f(&j->out, j->ctx, j->n);
            break;
        case 21:
            (void)zeta3_euler_f(&j->out, j->ctx, j->n);
            break;
        default:
            (void)gamma_bm_f(&j->out, j->ctx, j->n);
            break;
    }
    bigfloat_free(&t);
}


static void const_float(unsigned p) {
    bigfloat_ctx_ty ctx;
    if (bigfloat_ctx_make(&ctx, p, 61U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E) {
        return;
    }
    cjob_f j;
    memset(&j, 0, sizeof(j));
    j.ctx = &ctx;
    bigfloat_init(&j.one);
    bigfloat_init(&j.v1);
    bigfloat_init(&j.v2);
    bigfloat_init(&j.out);
    bigfrac_init(&j.eps);
    (void)bigfloat_from_f64(&j.one, 1.0);
    (void)bigfloat_from_f64(&j.v1, -1.0);
    (void)bigfloat_from_f64(&j.v2, 0.0);
    bigint_bin_ty one;
    bigint_bin_ty den;
    bigint_bin_init(&one);
    bigint_bin_init(&den);
    (void)bigint_bin_from_u64(&one, 1U);
    (void)bigint_bin_shl(&den, &one, (size_t)p + 20U);
    (void)bigfrac_from_ints(&j.eps, &one, &den);
    bigint_bin_free(&den);
    bigint_bin_free(&one);

    printf("const bigfloat p=%u\n", p);
    static const struct {
        const char *name;
        int op;
    } pi_ops[] = {
        {"pi_4atan1", 0}, {"pi_2asin1", 1}, {"pi_acos-1", 2},
        {"pi_2acos0", 3},
    };
    for (size_t i = 0U; i < sizeof(pi_ops) / sizeof(pi_ops[0]); i++) {
        if (budget_hit()) {
            printf("  (budget reached)\n");
            goto done;
        }
        j.op = pi_ops[i].op;
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", pi_ops[i].name, ms);
        json_measure("const", "pi", pi_ops[i].name, p, ms);
    }
    if (!budget_hit()) {
        /* Machin: 16·atan(1/5) − 4·atan(1/239) */
        (void)bigfloat_from_str(&j.v1, "0.2", &ctx, NULL);
        (void)bigfloat_from_str(&j.v2, "0.004184100418410041841", &ctx,
                NULL);
        j.op = 4;
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", "pi_machin", ms);
        json_measure("const", "pi", "pi_machin", p, ms);
    }
    if (!budget_hit()) {
        bigint_bin_ty pi;
        bigint_bin_init(&pi);
        const double t0 = now_ms();
        (void)nex_ball_pi_fixed_internal(&pi, p + 64U);
        const double ms = now_ms() - t0;
        printf("  %-14s %12.4f ms\n", "pi_chudnovsky", ms);
        json_measure("const", "pi", "pi_chudnovsky", p, ms);
        bigint_bin_free(&pi);
    }

    (void)bigfloat_from_f64(&j.v1, 2.0);
    (void)bigfloat_from_f64(&j.v2, 0.5);
    static const struct {
        const char *name;
        int op;
    } e_ops[] = {
        {"e_exp1", 10}, {"e_sqrt_exp2", 11}, {"e_exp05_sq", 12},
        {"e_series", 13},
    };
    for (size_t i = 0U; i < sizeof(e_ops) / sizeof(e_ops[0]); i++) {
        if (budget_hit()) {
            printf("  (budget reached)\n");
            goto done;
        }
        j.op = e_ops[i].op;
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", e_ops[i].name, ms);
        json_measure("const", "e", e_ops[i].name, p, ms);
    }

    /* ζ(3)：Apéry（float）；低精度附加 Euler 变换 */
    j.n = p / 2U + 16U;
    j.op = 20;
    if (!budget_hit()) {
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", "zeta3_apery", ms);
        json_measure("const", "zeta3", "zeta3_apery", p, ms);
    }
    if (p <= 512U && !budget_hit()) {
        j.n = 2U * p + 32U;
        j.op = 21;
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", "zeta3_euler", ms);
        json_measure("const", "zeta3", "zeta3_euler", p, ms);
    }

    /* γ：Brent–McMillan（n 与 2n 两种配置） */
    j.op = 30;
    j.n = (size_t)((p * 693U) / 4000U) + 8U;
    if (!budget_hit()) {
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", "gamma_bm", ms);
        json_measure("const", "gamma", "gamma_bm", p, ms);
    }
    j.n *= 2U;
    if (!budget_hit()) {
        const double ms = time_op(run_cjob_f, &j);
        printf("  %-14s %12.4f ms\n", "gamma_bm_2n", ms);
        json_measure("const", "gamma", "gamma_bm_2n", p, ms);
    }

done:
    bigfrac_free(&j.eps);
    bigfloat_free(&j.out);
    bigfloat_free(&j.v2);
    bigfloat_free(&j.v1);
    bigfloat_free(&j.one);
}

typedef struct {
    const bigdecimal_ctx_ty *ctx;
    bigdecimal_ty *out;
    size_t n;
    int op;   /* 0: zeta3 Apery, 1: gamma Brent-McMillan */
} dconst_job;

static void run_dconst(void *p) {
    dconst_job *j = (dconst_job *)p;
    if (j->op == 0) {
        (void)zeta3_apery_d(j->out, j->ctx, j->n);
    } else {
        (void)gamma_bm_d(j->out, j->ctx, j->n);
    }
}

static void const_decimal(unsigned d) {
    bigdecimal_ctx_ty ctx;
    if (bigdecimal_ctx_make(&ctx, d, 12U,
            BIGDECIMAL_ROUND_NEAREST_EVEN_E) != BIGDECIMAL_OK_E) {
        return;
    }
    printf("const bigdecimal p=%u\n", d);
    bigdecimal_ty out;
    bigdecimal_init(&out);
    if (!budget_hit()) {
        dconst_job job = {&ctx, &out,
                (size_t)((d * 3322U) / 1000U / 2U) + 16U, 0};
        const double ms = time_op(run_dconst, &job);
        printf("  %-14s %12.4f ms\n", "zeta3_apery", ms);
        json_measure("const", "zeta3", "zeta3_apery_dec", d, ms);
    }
    if (!budget_hit()) {
        const size_t n = (size_t)((d * 3322U) / 1000U * 693U / 4000U) + 8U;
        dconst_job job = {&ctx, &out, n, 1};
        const double ms = time_op(run_dconst, &job);
        printf("  %-14s %12.4f ms\n", "gamma_bm", ms);
        json_measure("const", "gamma", "gamma_bm_dec", d, ms);
    }
    bigdecimal_free(&out);
}


/* 首次运行的自检（p=128 / d=32），只打印不中断基准 */
static void const_check(void) {
    bigfloat_ctx_ty ctx;
    if (bigfloat_ctx_make(&ctx, 128U, 61U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E) {
        return;
    }
    const double want_pi = 3.14159265358979323846;
    const double want_e = 2.71828182845904523536;
    const double want_z3 = 1.20205690315959428540;
    const double want_g = 0.57721566490153286061;
    struct {
        const char *name;
        int op;
        size_t n;
        double want;
    } cases[] = {
        {"pi_4atan1", 0, 0U, want_pi},
        {"pi_2asin1", 1, 0U, want_pi},
        {"pi_acos-1", 2, 0U, want_pi},
        {"pi_2acos0", 3, 0U, want_pi},
        {"pi_machin", 4, 0U, want_pi},
        {"e_exp1", 10, 0U, want_e},
        {"e_sqrt_exp2", 11, 0U, want_e},
        {"e_exp05_sq", 12, 0U, want_e},
        {"e_series", 13, 0U, want_e},
        {"zeta3_apery", 20, 96U, want_z3},
        {"zeta3_euler", 21, 256U, want_z3},
        {"gamma_bm", 30, 64U, want_g},
    };
    cjob_f j;
    memset(&j, 0, sizeof(j));
    j.ctx = &ctx;
    bigfloat_init(&j.one);
    bigfloat_init(&j.v1);
    bigfloat_init(&j.v2);
    bigfloat_init(&j.out);
    bigfrac_init(&j.eps);
    (void)bigfloat_from_f64(&j.one, 1.0);
    (void)bigfloat_from_f64(&j.v1, 2.0);
    (void)bigfloat_from_f64(&j.v2, 0.5);
    bigint_bin_ty bi_one;
    bigint_bin_ty den;
    bigint_bin_init(&bi_one);
    bigint_bin_init(&den);
    (void)bigint_bin_from_u64(&bi_one, 1U);
    (void)bigint_bin_shl(&den, &bi_one, 148U);
    (void)bigfrac_from_ints(&j.eps, &bi_one, &den);
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        /* 与 const_float 相同的输入约定 */
        if (cases[i].op >= 10 && cases[i].op < 20) {
            (void)bigfloat_from_f64(&j.one, 1.0);
            (void)bigfloat_from_f64(&j.v1, 2.0);
            (void)bigfloat_from_f64(&j.v2, 0.5);
        } else if (cases[i].op == 2) {
            (void)bigfloat_from_f64(&j.v1, -1.0);
        } else if (cases[i].op == 3) {
            (void)bigfloat_from_f64(&j.v2, 0.0);
        } else if (cases[i].op == 4) {
            (void)bigfloat_from_str(&j.v1, "0.2", &ctx, NULL);
            (void)bigfloat_from_str(&j.v2,
                    "0.004184100418410041841", &ctx, NULL);
        }
        j.op = cases[i].op;
        j.n = cases[i].n;
        run_cjob_f(&j);
        double got = 0.0;
        (void)bigfloat_to_f64(&j.out, &got);
        if (!(fabs(got - cases[i].want) <= 1e-12)) {
            printf("CHECK FAIL %-14s got=%.17g want=%.17g\n", cases[i].name,
                   got, cases[i].want);
            g_fail = 1;
        }
    }
    bigfrac_free(&j.eps);
    bigfloat_free(&j.out);
    bigfloat_free(&j.v2);
    bigfloat_free(&j.v1);
    bigfloat_free(&j.one);
    bigint_bin_free(&den);
    bigint_bin_free(&bi_one);
    if (g_fail == 0) {
        printf("const self-check: OK\n");
    }
}

static void sweep_const(void) {
    size_t nf = 0U;
    size_t nd = 0U;
    const unsigned *fp = float_precs(&nf);
    const unsigned *dp = dec_precs(&nd);
    printf("\n== [const] 常数多表达式扫描 ==\n");
    const_check();
    for (size_t i = 0U; i < nf; i++) {
        if (budget_hit()) {
            printf("(budget reached)\n");
            return;
        }
        const_float(fp[i]);
    }
    for (size_t i = 0U; i < nd; i++) {
        if (budget_hit()) {
            printf("(budget reached)\n");
            return;
        }
        const_decimal(dp[i]);
    }
}

/* ------------------------------------------------------------------ */
/* 3) 吞吐量（ops/s）                                                  */
/* ------------------------------------------------------------------ */

static uint64_t g_rng = 0x20261018ULL;
static uint32_t rnd32(void) {
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(g_rng >> 32);
}

typedef struct {
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
} ibin_ctx;

static void ibin_fill(bigint_bin_ty *v, size_t limbs) {
    bigint_bin_init_cap(v, limbs);
    for (size_t i = 0U; i < limbs; i++) {
        v->limbs[i] = rnd32();
    }
    v->len = limbs;
    v->limbs[limbs - 1U] |= 1U;
}

static void run_bin_mul(void *p) {
    ibin_ctx *c = (ibin_ctx *)p;
    (void)bigint_bin_mul(&c->c, &c->a, &c->b);
}

static void run_bin_div(void *p) {
    ibin_ctx *c = (ibin_ctx *)p;
    (void)bigint_bin_div_rem(&c->c, NULL, &c->a, &c->b);
}

static void run_bin_gcd(void *p) {
    ibin_ctx *c = (ibin_ctx *)p;
    (void)bigint_bin_gcd(&c->c, &c->a, &c->b);
}

typedef struct {
    bigfloat_ctx_ty ctx;
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    int op;   /* 0 mul, 1 div, 2 sqrt, 3 exp, 4 log, 5 sin */
} bfloat_job;

static void run_bfloat_job(void *p) {
    bfloat_job *j = (bfloat_job *)p;
    switch (j->op) {
        case 0:
            (void)bigfloat_mul(&j->c, &j->a, &j->b, &j->ctx);
            break;
        case 1:
            (void)bigfloat_div(&j->c, &j->a, &j->b, &j->ctx);
            break;
        case 2:
            (void)bigfloat_sqrt(&j->c, &j->a, &j->ctx);
            break;
        case 3:
            (void)bigfloat_exp(&j->c, &j->a, &j->ctx);
            break;
        case 4:
            (void)bigfloat_log(&j->c, &j->a, &j->ctx);
            break;
        default:
            (void)bigfloat_sin(&j->c, &j->a, &j->ctx);
            break;
    }
}

typedef struct {
    bigdecimal_ctx_ty ctx;
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_ty c;
    int op;   /* 0 mul, 1 sqrt */
} bdec_job;

static void run_bdec_job(void *p) {
    bdec_job *j = (bdec_job *)p;
    if (j->op == 0) {
        (void)bigdecimal_mul(&j->c, &j->a, &j->b, &j->ctx);
    } else {
        (void)bigdecimal_sqrt(&j->c, &j->a, &j->ctx);
    }
}

static void print_through(const char *op, const char *unit, size_t size,
        double ms) {
    const double ops = 1000.0 / (ms > 0.0 ? ms : 1e-9);
    printf("  %-16s %8zu %-4s %12.4f ms %12.1f ops/s\n", op, size, unit, ms,
           ops);
    json_throughput(op, unit, size, ops);
}

static void sweep_throughput(void) {
    printf("\n== [throughput] 吞吐量（ops/s） ==\n");
    size_t nl = 0U;
    const size_t *limbs = through_limbs(&nl);
    for (size_t i = 0U; i < nl && !budget_hit(); i++) {
        const size_t n = limbs[i];
        ibin_ctx c;
        ibin_fill(&c.a, n);
        ibin_fill(&c.b, n);
        bigint_bin_init(&c.c);
        print_through("bigint_mul", "limb", n,
                time_op(run_bin_mul, &c));
        bigint_bin_free(&c.b);
        ibin_fill(&c.b, n / 2U + 1U);
        print_through("bigint_div", "limb", n,
                time_op(run_bin_div, &c));
        print_through("bigint_gcd", "limb", n,
                time_op(run_bin_gcd, &c));
        bigint_bin_free(&c.c);
        bigint_bin_free(&c.b);
        bigint_bin_free(&c.a);
    }

    size_t nb = 0U;
    const unsigned *bits = through_bits(&nb);
    for (size_t i = 0U; i < nb && !budget_hit(); i++) {
        const unsigned p = bits[i];
        bfloat_job j;
        memset(&j, 0, sizeof(j));
        if (bigfloat_ctx_make(&j.ctx, p, 61U,
                BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E) {
            continue;
        }
        bigfloat_init(&j.a);
        bigfloat_init(&j.b);
        bigfloat_init(&j.c);
        (void)bigfloat_from_str(&j.a, "1.234567890123456789", &j.ctx,
                NULL);
        (void)bigfloat_from_str(&j.b, "0.987654321098765432", &j.ctx,
                NULL);
        for (int op = 0; op <= 5; op++) {
            j.op = op;
            const char *name = (op == 0) ? "bigfloat_mul"
                    : (op == 1) ? "bigfloat_div"
                    : (op == 2) ? "bigfloat_sqrt"
                    : (op == 3) ? "bigfloat_exp"
                    : (op == 4) ? "bigfloat_log" : "bigfloat_sin";
            print_through(name, "bit", p, time_op(run_bfloat_job, &j));
        }
        bigfloat_free(&j.c);
        bigfloat_free(&j.b);
        bigfloat_free(&j.a);
    }

    size_t nd = 0U;
    const unsigned *digits = through_digits(&nd);
    for (size_t i = 0U; i < nd && !budget_hit(); i++) {
        const unsigned d = digits[i];
        bdec_job j;
        memset(&j, 0, sizeof(j));
        if (bigdecimal_ctx_make(&j.ctx, d, 12U,
                BIGDECIMAL_ROUND_NEAREST_EVEN_E) != BIGDECIMAL_OK_E) {
            continue;
        }
        bigdecimal_init(&j.a);
        bigdecimal_init(&j.b);
        bigdecimal_init(&j.c);
        (void)bigdecimal_from_str(&j.a, "1.234567890123456789", &j.ctx,
                NULL);
        (void)bigdecimal_from_str(&j.b, "0.987654321098765432", &j.ctx,
                NULL);
        j.op = 0;
        print_through("bigdecimal_mul", "digit", d,
                time_op(run_bdec_job, &j));
        j.op = 1;
        print_through("bigdecimal_sqrt", "digit", d,
                time_op(run_bdec_job, &j));
        bigdecimal_free(&j.c);
        bigdecimal_free(&j.b);
        bigdecimal_free(&j.a);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void usage(void) {
    printf("usage: bench_math [--preset full|quick|ci] "
           "[--sections all|trans,const,throughput] "
           "[--budget SECONDS] [--json FILE]\n");
}

int main(int argc, char **argv) {
    const char *sections = "all";
    const char *json_path = NULL;
    int budget_set = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "full") == 0) {
                g_preset = PRESET_FULL;
            } else if (strcmp(v, "quick") == 0) {
                g_preset = PRESET_QUICK;
            } else if (strcmp(v, "ci") == 0) {
                g_preset = PRESET_CI;
            } else {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--sections") == 0 && i + 1 < argc) {
            sections = argv[++i];
        } else if (strcmp(argv[i], "--budget") == 0 && i + 1 < argc) {
            g_budget_s = atof(argv[++i]);
            budget_set = 1;
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        } else {
            usage();
            return 2;
        }
    }
    if (!budget_set) {
        g_budget_s = (g_preset == PRESET_FULL) ? 7200.0
                : (g_preset == PRESET_QUICK) ? 600.0 : 300.0;
    }
    g_do_trans = (strcmp(sections, "all") == 0)
            || (strstr(sections, "trans") != NULL);
    g_do_const = (strcmp(sections, "all") == 0)
            || (strstr(sections, "const") != NULL);
    g_do_throughput = (strcmp(sections, "all") == 0)
            || (strstr(sections, "throughput") != NULL);
    if (json_path != NULL) {
        g_json = fopen(json_path, "w");
        if (g_json == NULL) {
            printf("无法写入 JSON: %s\n", json_path);
            return 2;
        }
        fprintf(g_json, "{\"meta\":\"bench_math\",\"preset\":\"%s\","
                "\"budget_s\":%.0f}\n",
                (g_preset == PRESET_FULL) ? "full"
                : (g_preset == PRESET_QUICK) ? "quick" : "ci",
                g_budget_s);
    }
    g_start_ms = now_ms();
    printf("bench_math preset=%s budget=%.0fs sections=%s\n",
            (g_preset == PRESET_FULL) ? "full"
            : (g_preset == PRESET_QUICK) ? "quick" : "ci",
            g_budget_s, sections);
    if (g_do_trans) {
        sweep_trans();
    }
    if (g_do_const && !budget_hit()) {
        sweep_const();
    }
    if (g_do_throughput && !budget_hit()) {
        sweep_throughput();
    }
    printf("\n总耗时 %.1f s\n", elapsed_s());
    if (g_json != NULL) {
        fclose(g_json);
    }
    return (g_fail == 0) ? 0 : 1;
}
