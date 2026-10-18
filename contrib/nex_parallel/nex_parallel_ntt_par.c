/*
 * nex_parallel_ntt_par.c：阶段级并行 NTT（contrib 实验）。
 *
 * 与 nex_ntt 的 forward/inverse 逐位一致：
 *   - 同样的 DIF/DIT 蝶形、Montgomery 模乘、wlen 推导与归一化顺序；
 *   - 每个 stage 把 n/2 个蝶形单元按扁平索引 u = k·(n/len) + t 均分给
 *     工作线程；不同 (k,t) 的 (j, j+half) 互不相交，无数据竞争；
 *   - 每个 task 从 wlen^k0 开始（Montgomery 幂），后续按 w *= wlen 递推。
 */

#include "nex_parallel_ntt_par.h"

#include <stdint.h>

#define PAR_MAX_TASKS 64U

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

static void lin_apply(uint32_t *coeffs, size_t len, uint32_t fac, uint32_t p,
        uint32_t p_inv, nex_thread_pool_ty *pool)
{
    unsigned nt = pool_tasks(pool);
    if ((size_t)nt > len) {
        nt = (unsigned)len;
    }
    if (nt == 0U) {
        return;
    }
    lin_job_ty jobs[PAR_MAX_TASKS];
    for (unsigned t = 0U; t < nt; t++) {
        jobs[t].coeffs = coeffs;
        jobs[t].begin = len * (size_t)t / (size_t)nt;
        jobs[t].end = len * (size_t)(t + 1U) / (size_t)nt;
        jobs[t].fac = fac;
        jobs[t].p = p;
        jobs[t].p_inv = p_inv;
    }
    nex_thread_pool_run(pool, nt, lin_job_fn, jobs);
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

static void run_stage(uint32_t *coeffs, uint32_t log2n, uint32_t len,
        uint32_t wlen, int inverse, uint32_t p, uint32_t p_inv, uint32_t r,
        nex_thread_pool_ty *pool)
{
    const uint32_t n = 1U << log2n;
    const uint32_t units = n / 2U;
    unsigned nt = pool_tasks(pool);
    if ((uint32_t)nt > units) {
        nt = units;
    }
    if (nt == 0U) {
        return;
    }
    stage_job_ty jobs[PAR_MAX_TASKS];
    for (unsigned t = 0U; t < nt; t++) {
        jobs[t].coeffs = coeffs;
        jobs[t].n = n;
        jobs[t].len = len;
        jobs[t].half = len >> 1U;
        jobs[t].per_k = n / len;
        jobs[t].u0 = (uint32_t)((uint64_t)units * t / nt);
        jobs[t].u1 = (uint32_t)((uint64_t)units * (t + 1U) / nt);
        jobs[t].p = p;
        jobs[t].p_inv = p_inv;
        jobs[t].wlen = wlen;
        jobs[t].r = r;
        jobs[t].inverse = inverse;
    }
    nex_thread_pool_run(pool, nt, stage_job_fn, jobs);
}

void nex_par_ntt_forward(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool)
{
    if ((pool == NULL) || (nex_thread_pool_count(pool) <= 1U)) {
        (void)ntt_forward(coeffs, log2n, mod);
        return;
    }
    const uint32_t p = mod->p;
    const uint32_t n = 1U << log2n;
    const uint32_t p_inv = 0U - par_inv_pow2(p);
    const uint32_t r = (uint32_t)((UINT64_C(1) << 32) % p);
    const uint32_t r2 = (uint32_t)((uint64_t)r * r % p);

    lin_apply(coeffs, n, r2, p, p_inv, pool);
    for (uint32_t s = log2n; s >= 1U; s--) {
        const uint32_t len = 1U << s;
        const uint32_t wlen = par_mont_mul(
                ntt_mod_pow(mod->w, 1U << (mod->c - s), mod), r2, p, p_inv);
        run_stage(coeffs, log2n, len, wlen, 0, p, p_inv, r, pool);
    }
    lin_apply(coeffs, n, 1U, p, p_inv, pool);
}

void nex_par_ntt_inverse(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool)
{
    if ((pool == NULL) || (nex_thread_pool_count(pool) <= 1U)) {
        (void)ntt_inverse(coeffs, log2n, mod);
        return;
    }
    const uint32_t p = mod->p;
    const uint32_t n = 1U << log2n;
    const uint32_t p_inv = 0U - par_inv_pow2(p);
    const uint32_t r = (uint32_t)((UINT64_C(1) << 32) % p);
    const uint32_t r2 = (uint32_t)((uint64_t)r * r % p);
    const uint32_t root_inv = ntt_mod_inv(
            ntt_mod_pow(mod->w, 1U << (mod->c - log2n), mod), mod);

    lin_apply(coeffs, n, r2, p, p_inv, pool);
    for (uint32_t s = 1U; s <= log2n; s++) {
        const uint32_t len = 1U << s;
        const uint32_t wlen = par_mont_mul(
                ntt_mod_pow(root_inv, n / len, mod), r2, p, p_inv);
        run_stage(coeffs, log2n, len, wlen, 1, p, p_inv, r, pool);
    }
    const uint32_t n_inv = ntt_mod_inv(n, mod);
    const uint32_t n_inv_m = par_mont_mul(n_inv, r2, p, p_inv);
    lin_apply(coeffs, n, n_inv_m, p, p_inv, pool);
    lin_apply(coeffs, n, 1U, p, p_inv, pool);
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

void nex_par_ntt_pointwise(uint32_t *dst, const uint32_t *lhs,
        const uint32_t *rhs, size_t len, const ntt_mod_ty *mod,
        nex_thread_pool_ty *pool)
{
    if ((pool == NULL) || (nex_thread_pool_count(pool) <= 1U)) {
        (void)ntt_pointwise_mul(dst, lhs, rhs, len, mod);
        return;
    }
    unsigned nt = pool_tasks(pool);
    if ((size_t)nt > len) {
        nt = (unsigned)len;
    }
    if (nt == 0U) {
        return;
    }
    pw_job_ty jobs[PAR_MAX_TASKS];
    for (unsigned t = 0U; t < nt; t++) {
        jobs[t].dst = dst;
        jobs[t].lhs = lhs;
        jobs[t].rhs = rhs;
        jobs[t].begin = len * (size_t)t / (size_t)nt;
        jobs[t].end = len * (size_t)(t + 1U) / (size_t)nt;
        jobs[t].p = mod->p;
    }
    nex_thread_pool_run(pool, nt, pw_job_fn, jobs);
}
