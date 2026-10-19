/*
 * examples/parallel_vector/parallel_vector.c：contrib 线程池消费方 B。
 *
 * 对 N 个独立 bigfloat 做逐元素 out[i] = a[i]·b[i] + c[i]：
 *   - 串行基线 vs 1/2/4/8/16 线程的 parallel-for（固定分块，结果可复现）；
 *   - 每个元素不同对象，符合 design §3.3（不同线程操作不同对象）；
 *   - 仅作为“大批量小/中规模运算”的线程收益演示，不进入核心。
 *
 * 构建：-DNEX_ENABLE_THREAD=ON（默认 OFF）。
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nex/bigfloat/nex_bigfloat.h"
#include "nex_thread.h"

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

typedef struct {
    bigfloat_ty *a;
    bigfloat_ty *b;
    bigfloat_ty *c;
    bigfloat_ty *out;
    const bigfloat_ctx_ty *ctx;
    size_t count;
} vec_job_ty;

static void vec_job_fn(void *ctx, size_t index)
{
    vec_job_ty *j = (vec_job_ty *)ctx;
    const size_t chunk = 256U;
    const size_t begin = index * chunk;
    const size_t end = (begin + chunk > j->count) ? j->count : begin + chunk;
    for (size_t i = begin; i < end; i++) {
        bigfloat_mul(&j->out[i], &j->a[i], &j->b[i], j->ctx);
        bigfloat_add(&j->out[i], &j->out[i], &j->c[i], j->ctx);
    }
}

static int parse_template(bigfloat_ty *v, size_t bits,
        const bigfloat_ctx_ty *ctx, double seed)
{
    const size_t nd = bits / 3U + 16U;
    char *s = (char *)malloc(nd + 8U);
    if (s == NULL) {
        return -1;
    }
    s[0] = '1';
    s[1] = '.';
    for (size_t k = 2U; k < nd; k++) {
        s[k] = (char)('0' + (int)((k * 7U + (size_t)seed) % 10U));
    }
    s[nd] = '\0';
    const int rc = (bigfloat_from_str(v, s, ctx, NULL) == BIGFLOAT_OK_E)
            ? 0 : -1;
    free(s);
    return rc;
}

/* 用高精度模板 × 小整数初始化，保证每个元素有 p 位尾数且取值不同 */
static int init_array(bigfloat_ty *arr, size_t count, size_t bits,
        const bigfloat_ctx_ty *ctx, double seed)
{
    bigfloat_ty tpl;
    bigfloat_init(&tpl);
    if (parse_template(&tpl, bits, ctx, seed) != 0) {
        return -1;
    }
    bigfloat_ty k;
    bigfloat_init(&k);
    int rc = 0;
    for (size_t i = 0U; i < count; i++) {
        bigfloat_from_f64(&k, (double)((i % 97U) + 1U));
        if ((bigfloat_mul(&arr[i], &tpl, &k, ctx) != BIGFLOAT_OK_E)) {
            rc = -1;
            break;
        }
    }
    bigfloat_free(&k);
    bigfloat_free(&tpl);
    return rc;
}

static double time_serial(const vec_job_ty *j, int reps)
{
    double best = 1e30;
    for (int t = 0; t < reps; t++) {
        const double t0 = now_ms();
        for (size_t i = 0U; i < j->count; i++) {
            bigfloat_mul(&j->out[i], &j->a[i], &j->b[i], j->ctx);
            bigfloat_add(&j->out[i], &j->out[i], &j->c[i], j->ctx);
        }
        const double dt = now_ms() - t0;
        if (dt < best) {
            best = dt;
        }
    }
    return best;
}

static double time_par(const vec_job_ty *j, nex_thread_pool_ty *pool,
        int reps)
{
    const size_t njobs = (j->count + 255U) / 256U;
    double best = 1e30;
    for (int t = 0; t < reps; t++) {
        const double t0 = now_ms();
        nex_thread_pool_run(pool, njobs, vec_job_fn, (void *)j);
        const double dt = now_ms() - t0;
        if (dt < best) {
            best = dt;
        }
    }
    return best;
}

int main(int argc, char **argv)
{
    const size_t bits = (argc > 1) ? (size_t)strtoul(argv[1], NULL, 10)
            : 1024U;
    const size_t count = (argc > 2) ? (size_t)strtoul(argv[2], NULL, 10)
            : 20000U;
    static const unsigned thread_counts[] = { 1U, 2U, 4U, 8U, 16U };

    bigfloat_ctx_ty ctx;
    if (bigfloat_ctx_make(&ctx, bits, 32U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E) {
        printf("invalid ctx\n");
        return 1;
    }

    bigfloat_ty *a = (bigfloat_ty *)calloc(count, sizeof(bigfloat_ty));
    bigfloat_ty *b = (bigfloat_ty *)calloc(count, sizeof(bigfloat_ty));
    bigfloat_ty *c = (bigfloat_ty *)calloc(count, sizeof(bigfloat_ty));
    bigfloat_ty *out = (bigfloat_ty *)calloc(count, sizeof(bigfloat_ty));
    bigfloat_ty *ref = (bigfloat_ty *)calloc(count, sizeof(bigfloat_ty));
    if ((a == NULL) || (b == NULL) || (c == NULL) || (out == NULL)
            || (ref == NULL)) {
        printf("OOM\n");
        return 1;
    }
    for (size_t i = 0U; i < count; i++) {
        bigfloat_init(&a[i]);
        bigfloat_init(&b[i]);
        bigfloat_init(&c[i]);
        bigfloat_init(&out[i]);
        bigfloat_init(&ref[i]);
    }
    if ((init_array(a, count, bits, &ctx, 1.0) != 0)
            || (init_array(b, count, bits, &ctx, 4.0) != 0)
            || (init_array(c, count, bits, &ctx, 7.0) != 0)) {
        printf("init failed\n");
        return 1;
    }

    vec_job_ty job;
    job.a = a;
    job.b = b;
    job.c = c;
    job.out = out;
    job.ctx = &ctx;
    job.count = count;

    /* 串行参考（同时作为 out 的期望值） */
    const int reps = (bits <= 1024U) ? 3 : 2;
    const double serial_ms = time_serial(&job, reps);
    for (size_t i = 0U; i < count; i++) {
        bigfloat_mul(&ref[i], &a[i], &b[i], &ctx);
        bigfloat_add(&ref[i], &ref[i], &c[i], &ctx);
    }

    printf("parallel vector: bits=%zu count=%zu op=out=a*b+c\n",
            bits, count);
    printf("%10s %10s %10s %9s %6s\n",
            "threads", "par_ms", "speedup", "serial_ms", "match");
    for (size_t t = 0U; t < sizeof(thread_counts) / sizeof(thread_counts[0]);
            t++) {
        nex_thread_pool_ty *pool = nex_thread_pool_create(thread_counts[t]);
        if (pool == NULL) {
            printf("%10u %10s %10s %10s %6s\n", thread_counts[t], "-", "-",
                    "-", "pool-fail");
            continue;
        }
        const double par_ms = time_par(&job, pool, reps);
        int match = 1;
        for (size_t i = 0U; i < count; i++) {
            if (bigfloat_cmp(&out[i], &ref[i]) != 0) {
                match = 0;
                break;
            }
        }
        printf("%10u %10.3f %9.2fx %10.3f %6s\n", thread_counts[t], par_ms,
                (par_ms > 0.0) ? (serial_ms / par_ms) : 0.0, serial_ms,
                match ? "ok" : "FAIL");
        nex_thread_pool_destroy(pool);
    }

    for (size_t i = 0U; i < count; i++) {
        bigfloat_free(&a[i]);
        bigfloat_free(&b[i]);
        bigfloat_free(&c[i]);
        bigfloat_free(&out[i]);
        bigfloat_free(&ref[i]);
    }
    free(a);
    free(b);
    free(c);
    free(out);
    free(ref);
    return 0;
}
