/*
 * nex_parallel_ntt_par.c：阶段级并行 NTT（contrib 实验）。
 *
 * 多缓冲区版本：每个 stage 只做一次 barrier，把 count 个缓冲区的蝶形单元
 * 一起分给线程池；不同 (buffer,k,t) 的 (j,j+half) 互不相交，无数据竞争。
 * 与 nex_ntt 的 DIF/DIT + Montgomery 实现逐位一致。
 *
 * 结果与线程数/分块无关：每个蝶形单元的计算是独立的，不涉及归约。
 */

#include "nex_parallel_ntt_par.h"

#include <stdint.h>

#define PAR_MAX_TASKS 64U
#define PAR_MAX_BUFS 8U
#define PAR_MAX_JOBS 128U

static uint32_t par_inv_pow2(uint32_t p)
{
    uint32_t x = p;
    x = x * (2U - p * x);
    x = x * (2U - p * x);
    x = x * (2U - p * x);
    x = x * (2U - p * x);
    x = x * (2U - p * x);
    return x;
}

static uint32_t par_mont_mul(uint32_t a, uint32_t b, uint32_t p,
        uint32_t p_inv)
{
    const uint64_t t = (uint64_t)a * b;
    const uint32_t t_lo = (uint32_t)t;
    const uint32_t t_hi = (uint32_t)(t >> 32U);
    const uint32_t m = t_lo * p_inv;
    const uint64_t mp = (uint64_t)m * p;
    const uint32_t mp_lo = (uint32_t)mp;
    const uint32_t mp_hi = (uint32_t)(mp >> 32U);
    const uint32_t carry = (t_lo + mp_lo < t_lo) ? 1U : 0U;
    const uint64_t u64 = (uint64_t)t_hi + mp_hi + carry;
    return (uint32_t)(u64 - ((u64 >= p) ? p : 0U));
}

static uint32_t par_mont_pow(uint32_t base, uint32_t exp, uint32_t p,
        uint32_t p_inv, uint32_t r)
{
    uint32_t acc = r;
    uint32_t b = base;
    while (exp != 0U) {
        if ((exp & 1U) != 0U) {
            acc = par_mont_mul(acc, b, p, p_inv);
        }
        b = par_mont_mul(b, b, p, p_inv);
        exp >>= 1U;
    }
    return acc;
}

static uint32_t par_bf_sum(uint32_t u, uint32_t v, uint32_t p)
{
    const uint64_t uv = (uint64_t)u + v;
    return (uint32_t)(uv - ((uv >= p) ? (uint64_t)p : 0U));
}

static uint32_t par_bf_diff(uint32_t u, uint32_t v, uint32_t p)
{
    return (u >= v) ? (u - v) : (u + (p - v));
}

static unsigned pool_tasks(nex_thread_pool_ty *pool)
{
    unsigned n = (pool != NULL) ? nex_thread_pool_count(pool) : 1U;
    if (n == 0U) {
        n = 1U;
    }
    if (n > PAR_MAX_TASKS) {
        n = PAR_MAX_TASKS;
    }
    return n;
}

static unsigned tasks_per_buf(unsigned tasks, size_t count)
{
    const unsigned t = tasks / (unsigned)count;
    return (t == 0U) ? 1U : t;
}

typedef struct {
    uint32_t *coeffs;
    size_t begin;
    size_t end;
    uint32_t fac;
    uint32_t p;
    uint32_t p_inv;
} lin_job_ty;

static void lin_job_fn(void *ctx, size_t index)
{
    const lin_job_ty *j = &((const lin_job_ty *)ctx)[index];
    for (size_t i = j->begin; i < j->end; i++) {
        j->coeffs[i] = par_mont_mul(j->coeffs[i], j->fac, j->p, j->p_inv);
    }
}

static void run_lin_multi(nex_par_ntt_buf_ty *bufs, size_t count, size_t len,
        const uint32_t *fac, const uint32_t *p, const uint32_t *p_inv,
        unsigned tpb, nex_thread_pool_ty *pool)
{
    lin_job_ty jobs[PAR_MAX_JOBS];
    size_t nj = 0U;
    for (size_t b = 0U; b < count; b++) {
        for (unsigned t = 0U; t < tpb; t++) {
            jobs[nj].coeffs = bufs[b].buf;
            jobs[nj].begin = len * (size_t)t / (size_t)tpb;
            jobs[nj].end = len * (size_t)(t + 1U) / (size_t)tpb;
            jobs[nj].fac = fac[b];
            jobs[nj].p = p[b];
            jobs[nj].p_inv = p_inv[b];
            nj++;
        }
    }
    nex_thread_pool_run(pool, nj, lin_job_fn, jobs);
}

typedef struct {
    uint32_t *coeffs;
    uint32_t n;
    uint32_t len;
    uint32_t half;
    uint32_t per_k;
    uint32_t u0;
    uint32_t u1;
    uint32_t p;
    uint32_t p_inv;
    uint32_t wlen;
    uint32_t r;
    int inverse;
} stage_job_ty;

static void stage_job_fn(void *ctx, size_t index)
{
    const stage_job_ty *j = &((const stage_job_ty *)ctx)[index];
    uint32_t u = j->u0;
    uint32_t k = u / j->per_k;
    uint32_t t = u % j->per_k;
    uint32_t w = par_mont_pow(j->wlen, k, j->p, j->p_inv, j->r);

    while (u < j->u1) {
        const uint32_t pos = k + t * j->len;
        const uint32_t uval = j->coeffs[pos];
        if (j->inverse == 0) {
            const uint32_t vval = j->coeffs[pos + j->half];
            j->coeffs[pos] = par_bf_sum(uval, vval, j->p);
            j->coeffs[pos + j->half] = par_mont_mul(
                    par_bf_diff(uval, vval, j->p), w, j->p, j->p_inv);
        } else {
            const uint32_t vval = par_mont_mul(j->coeffs[pos + j->half], w,
                    j->p, j->p_inv);
            j->coeffs[pos] = par_bf_sum(uval, vval, j->p);
            j->coeffs[pos + j->half] = par_bf_diff(uval, vval, j->p);
        }
        u++;
        t++;
        if (t == j->per_k) {
            t = 0U;
            k++;
            if (u < j->u1) {
                w = par_mont_mul(w, j->wlen, j->p, j->p_inv);
            }
        }
    }
}

static void run_stage_multi(nex_par_ntt_buf_ty *bufs, size_t count,
        uint32_t log2n, uint32_t len, const uint32_t *wlen,
        const uint32_t *p, const uint32_t *p_inv, const uint32_t *r,
        int inverse, unsigned tpb, nex_thread_pool_ty *pool)
{
    const uint32_t n = 1U << log2n;
    const uint32_t units = n / 2U;
    stage_job_ty jobs[PAR_MAX_JOBS];
    size_t nj = 0U;
    for (size_t b = 0U; b < count; b++) {
        for (unsigned t = 0U; t < tpb; t++) {
            jobs[nj].coeffs = bufs[b].buf;
            jobs[nj].n = n;
            jobs[nj].len = len;
            jobs[nj].half = len >> 1U;
            jobs[nj].per_k = n / len;
            jobs[nj].u0 = (uint32_t)((uint64_t)units * t / tpb);
            jobs[nj].u1 = (uint32_t)((uint64_t)units * (t + 1U) / tpb);
            jobs[nj].p = p[b];
            jobs[nj].p_inv = p_inv[b];
            jobs[nj].wlen = wlen[b];
            jobs[nj].r = r[b];
            jobs[nj].inverse = inverse;
            nj++;
        }
    }
    nex_thread_pool_run(pool, nj, stage_job_fn, jobs);
}

void nex_par_ntt_forward_multi(nex_par_ntt_buf_ty *bufs, size_t count,
        uint32_t log2n, nex_thread_pool_ty *pool)
{
    if ((bufs == NULL) || (count == 0U)) {
        return;
    }
    if ((count > PAR_MAX_BUFS) || (pool == NULL)
            || (nex_thread_pool_count(pool) <= 1U)) {
        for (size_t b = 0U; b < count; b++) {
            (void)ntt_forward(bufs[b].buf, log2n, bufs[b].mod);
        }
        return;
    }

    uint32_t p[PAR_MAX_BUFS];
    uint32_t p_inv[PAR_MAX_BUFS];
    uint32_t r[PAR_MAX_BUFS];
    uint32_t r2[PAR_MAX_BUFS];
    uint32_t one[PAR_MAX_BUFS];
    const uint32_t n = 1U << log2n;
    for (size_t b = 0U; b < count; b++) {
        p[b] = bufs[b].mod->p;
        p_inv[b] = 0U - par_inv_pow2(p[b]);
        r[b] = (uint32_t)((UINT64_C(1) << 32) % p[b]);
        r2[b] = (uint32_t)((uint64_t)r[b] * r[b] % p[b]);
        one[b] = 1U;
    }
    const unsigned tpb = tasks_per_buf(pool_tasks(pool), count);

    run_lin_multi(bufs, count, n, r2, p, p_inv, tpb, pool);
    for (uint32_t s = log2n; s >= 1U; s--) {
        const uint32_t len = 1U << s;
        uint32_t wlen[PAR_MAX_BUFS];
        for (size_t b = 0U; b < count; b++) {
            wlen[b] = par_mont_mul(
                    ntt_mod_pow(bufs[b].mod->w, 1U << (bufs[b].mod->c - s),
                            bufs[b].mod),
                    r2[b], p[b], p_inv[b]);
        }
        run_stage_multi(bufs, count, log2n, len, wlen, p, p_inv, r, 0, tpb,
                pool);
    }
    run_lin_multi(bufs, count, n, one, p, p_inv, tpb, pool);
}

void nex_par_ntt_inverse_multi(nex_par_ntt_buf_ty *bufs, size_t count,
        uint32_t log2n, nex_thread_pool_ty *pool)
{
    if ((bufs == NULL) || (count == 0U)) {
        return;
    }
    if ((count > PAR_MAX_BUFS) || (pool == NULL)
            || (nex_thread_pool_count(pool) <= 1U)) {
        for (size_t b = 0U; b < count; b++) {
            (void)ntt_inverse(bufs[b].buf, log2n, bufs[b].mod);
        }
        return;
    }

    uint32_t p[PAR_MAX_BUFS];
    uint32_t p_inv[PAR_MAX_BUFS];
    uint32_t r[PAR_MAX_BUFS];
    uint32_t r2[PAR_MAX_BUFS];
    uint32_t root_inv[PAR_MAX_BUFS];
    uint32_t n_inv_m[PAR_MAX_BUFS];
    uint32_t one[PAR_MAX_BUFS];
    const uint32_t n = 1U << log2n;
    for (size_t b = 0U; b < count; b++) {
        p[b] = bufs[b].mod->p;
        p_inv[b] = 0U - par_inv_pow2(p[b]);
        r[b] = (uint32_t)((UINT64_C(1) << 32) % p[b]);
        r2[b] = (uint32_t)((uint64_t)r[b] * r[b] % p[b]);
        root_inv[b] = ntt_mod_inv(
                ntt_mod_pow(bufs[b].mod->w,
                        1U << (bufs[b].mod->c - log2n), bufs[b].mod),
                bufs[b].mod);
        const uint32_t n_inv = ntt_mod_inv(n, bufs[b].mod);
        n_inv_m[b] = par_mont_mul(n_inv, r2[b], p[b], p_inv[b]);
        one[b] = 1U;
    }
    const unsigned tpb = tasks_per_buf(pool_tasks(pool), count);

    run_lin_multi(bufs, count, n, r2, p, p_inv, tpb, pool);
    for (uint32_t s = 1U; s <= log2n; s++) {
        const uint32_t len = 1U << s;
        uint32_t wlen[PAR_MAX_BUFS];
        for (size_t b = 0U; b < count; b++) {
            wlen[b] = par_mont_mul(
                    ntt_mod_pow(root_inv[b], n / len, bufs[b].mod),
                    r2[b], p[b], p_inv[b]);
        }
        run_stage_multi(bufs, count, log2n, len, wlen, p, p_inv, r, 1, tpb,
                pool);
    }
    run_lin_multi(bufs, count, n, n_inv_m, p, p_inv, tpb, pool);
    run_lin_multi(bufs, count, n, one, p, p_inv, tpb, pool);
}

typedef struct {
    uint32_t *dst;
    const uint32_t *lhs;
    const uint32_t *rhs;
    size_t begin;
    size_t end;
    uint32_t p;
} pw_job_ty;

static void pw_job_fn(void *ctx, size_t index)
{
    const pw_job_ty *j = &((const pw_job_ty *)ctx)[index];
    for (size_t i = j->begin; i < j->end; i++) {
        j->dst[i] = (uint32_t)((uint64_t)j->lhs[i] * j->rhs[i] % j->p);
    }
}

void nex_par_ntt_pointwise_multi(nex_par_ntt_pw_ty *pw, size_t count,
        size_t len, nex_thread_pool_ty *pool)
{
    if ((pw == NULL) || (count == 0U)) {
        return;
    }
    if ((count > PAR_MAX_BUFS) || (pool == NULL)
            || (nex_thread_pool_count(pool) <= 1U)) {
        for (size_t b = 0U; b < count; b++) {
            (void)ntt_pointwise_mul(pw[b].dst, pw[b].lhs, pw[b].rhs, len,
                    pw[b].mod);
        }
        return;
    }
    const unsigned tpb = tasks_per_buf(pool_tasks(pool), count);
    pw_job_ty jobs[PAR_MAX_JOBS];
    size_t nj = 0U;
    for (size_t b = 0U; b < count; b++) {
        for (unsigned t = 0U; t < tpb; t++) {
            jobs[nj].dst = pw[b].dst;
            jobs[nj].lhs = pw[b].lhs;
            jobs[nj].rhs = pw[b].rhs;
            jobs[nj].begin = len * (size_t)t / (size_t)tpb;
            jobs[nj].end = len * (size_t)(t + 1U) / (size_t)tpb;
            jobs[nj].p = pw[b].mod->p;
            nj++;
        }
    }
    nex_thread_pool_run(pool, nj, pw_job_fn, jobs);
}

void nex_par_ntt_forward(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool)
{
    nex_par_ntt_buf_ty b = { coeffs, mod };
    nex_par_ntt_forward_multi(&b, 1U, log2n, pool);
}

void nex_par_ntt_inverse(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool)
{
    nex_par_ntt_buf_ty b = { coeffs, mod };
    nex_par_ntt_inverse_multi(&b, 1U, log2n, pool);
}

void nex_par_ntt_pointwise(uint32_t *dst, const uint32_t *lhs,
        const uint32_t *rhs, size_t len, const ntt_mod_ty *mod,
        nex_thread_pool_ty *pool)
{
    nex_par_ntt_pw_ty j = { dst, lhs, rhs, mod };
    nex_par_ntt_pointwise_multi(&j, 1U, len, pool);
}
