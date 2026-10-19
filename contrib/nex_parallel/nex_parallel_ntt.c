/*
 * nex_parallel_ntt.c：contrib 层并行多模数 CRT NTT 乘法（TODO §F 实验）。
 *
 * 纯调用方实现：不修改核心；复用 nex_ntt 的公开 API 与 bigint_bin 类型。
 * 与核心 mul_ntt 的编排一致（双模数、16-bit 分节、Garner CRT + base-2^16
 * 进位）。并行粒度：
 *   - n < 65536：4 路任务（两个模数 × a/b 的正变换）+ 2 路点乘/逆变换；
 *   - n ≥ 65536：扁平任务图——每个 stage 一次 barrier，同时处理
 *     a0/b0/a1/b1 四个缓冲区；点乘/逆变换同理；
 *   - Garner 系数重构按 k 并行，进位串行。
 */

#include "nex_parallel_ntt.h"
#include "nex_parallel_ntt_par.h"
#include "nex/ntt/nex_ntt.h"

#include <stdlib.h>
#include <string.h>

/* 与核心 NEX_MUL_NTT_MAX_SUM 一致：len(lhs)+len(rhs) ≤ 2^26 */
#define PAR_NTT_MAX_SUM (1U << 26U)

static void par_normalize(bigint_bin_ty *v)
{
    while ((v->len > 0U) && (v->limbs[v->len - 1U] == 0U)) {
        v->len--;
    }
    if (v->len == 0U) {
        v->sign = BIGINT_SIGN_ZERO_E;
    }
}

static bigint_err_ty par_pick_mods(uint32_t log2n,
        const ntt_mod_ty *mods[2])
{
    size_t found = 0U;
    const size_t count = ntt_mod_count();
    for (size_t i = 0U; i < count; i++) {
        const ntt_mod_ty *mod = ntt_mod_get(i);
        if (mod->c >= log2n) {
            mods[found] = mod;
            found++;
            if (found == 2U) {
                return BIGINT_OK_E;
            }
        }
    }
    return BIGINT_ERR_UNSUPPORTED_E;
}

/* 幅值拆 16-bit 节并零填充至长度 n（2 节/肢） */
static void par_pack(uint32_t *out, size_t n, const bigint_bin_ty *v)
{
    memset(out, 0, n * sizeof(uint32_t));
    for (size_t i = 0U; i < v->len; i++) {
        const uint32_t limb = v->limbs[i];
        out[2U * i] = limb & 0xFFFFU;
        out[2U * i + 1U] = limb >> 16U;
    }
}

/* 小变换长度（n < 131072）用“跨模数/跨 a,b 的 4 路核心 NTT”并行：
 * 每次调用核心 ntt_forward 不占额外线程，避免 stage 级屏障开销。 */
#define PAR_STAGE_MIN_N 65536U

typedef struct {
    uint32_t *buf;
    uint32_t log2n;
    const ntt_mod_ty *mod;
    ntt_err_ty err;
} core_fwd_job_ty;

static void core_fwd_fn(void *ctx, size_t index)
{
    core_fwd_job_ty *j = &((core_fwd_job_ty *)ctx)[index];
    j->err = ntt_forward(j->buf, j->log2n, j->mod);
}

typedef struct {
    uint32_t *dst;
    const uint32_t *lhs;
    const uint32_t *rhs;
    size_t len;
    const ntt_mod_ty *mod;
    ntt_err_ty err;
} core_pw_job_ty;

static void core_pw_fn(void *ctx, size_t index)
{
    core_pw_job_ty *j = &((core_pw_job_ty *)ctx)[index];
    j->err = ntt_pointwise_mul(j->dst, j->lhs, j->rhs, j->len, j->mod);
}

typedef struct {
    uint32_t *buf;
    uint32_t log2n;
    const ntt_mod_ty *mod;
    ntt_err_ty err;
} core_inv_job_ty;

static void core_inv_fn(void *ctx, size_t index)
{
    core_inv_job_ty *j = &((core_inv_job_ty *)ctx)[index];
    j->err = ntt_inverse(j->buf, j->log2n, j->mod);
}

typedef struct {
    const uint32_t *r1;
    const uint32_t *r2;
    uint64_t *coeff;
    uint32_t p1;
    uint32_t p2;
    uint32_t p1_inv;
    size_t begin;
    size_t end;
} crt_job_ty;

static void crt_job_fn(void *ctx, size_t index)
{
    const crt_job_ty *j = &((const crt_job_ty *)ctx)[index];
    for (size_t k = j->begin; k < j->end; k++) {
        const uint32_t r1 = j->r1[k];
        const uint32_t r2 = j->r2[k];
        const uint32_t r1_mod = (uint32_t)((uint64_t)r1 % j->p2);
        const uint32_t diff = (r2 >= r1_mod)
                ? (r2 - r1_mod) : (r2 + j->p2 - r1_mod);
        const uint32_t t2 = (uint32_t)((uint64_t)diff * j->p1_inv % j->p2);
        j->coeff[k] = (uint64_t)r1 + (uint64_t)j->p1 * t2;
    }
}

/* 分块进位：4 个 base-2^16 系数为一个块，块内用 (hi,lo) 128 位归约，
 * 块间前缀串行；块归约本身并行。 */
typedef struct {
    const uint64_t *coeff;
    uint64_t *low;
    uint64_t *high;
    size_t b0;
    size_t b1;
} blk_job_ty;

static void blk_reduce(const uint64_t *coeff, size_t b, uint64_t *low,
        uint64_t *high)
{
    uint64_t hi = 0U;
    uint64_t lo = 0U;
    for (int j = 3; j >= 0; j--) {
        /* (hi,lo) *= 2^16 */
        hi = (hi << 16U) | (lo >> 48U);
        lo <<= 16U;
        /* += coeff[4b+j]（< 2^60，低字相加最多进位 1） */
        const uint64_t c = coeff[4U * b + (size_t)j];
        const uint64_t old = lo;
        lo = old + c;
        if (lo < old) {
            hi++;
        }
    }
    *low = lo;
    *high = hi;
}

static void blk_job_fn(void *ctx, size_t index)
{
    const blk_job_ty *j = &((const blk_job_ty *)ctx)[index];
    for (size_t b = j->b0; b < j->b1; b++) {
        blk_reduce(j->coeff, b, &j->low[b], &j->high[b]);
    }
}

bigint_err_ty nex_parallel_bin_mul_ntt(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs,
        nex_thread_pool_ty *pool)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((lhs->len == 0U) || (rhs->len == 0U)) {
        return bigint_bin_from_u64(dst, 0U);
    }
    const size_t s = lhs->len + rhs->len;
    if (s > PAR_NTT_MAX_SUM) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    const size_t chunks_side = 2U * max_len;
    const size_t need = (2U * s - 1U > chunks_side)
            ? (2U * s - 1U) : chunks_side;
    uint32_t log2n = 0U;
    while ((log2n < 31U) && (((size_t)1U << log2n) < need)) {
        log2n++;
    }
    const uint32_t n = 1U << log2n;

    const ntt_mod_ty *mods[2];
    if (par_pick_mods(log2n, mods) != BIGINT_OK_E) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }

    uint32_t *a0 = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    uint32_t *b0 = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    uint32_t *a1 = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    uint32_t *b1 = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    if ((a0 == NULL) || (b0 == NULL) || (a1 == NULL) || (b1 == NULL)) {
        free(a1);
        free(b1);
        free(a0);
        free(b0);
        return BIGINT_ERR_OOM_E;
    }

    bigint_bin_ty out;
    bigint_err_ty err = bigint_bin_init_cap(&out, s);
    if (err != BIGINT_OK_E) {
        free(a1);
        free(b1);
        free(a0);
        free(b0);
        return err;
    }

    /* 两个模数共享同一组 16-bit 节；变换会就地改写，故各持一份 */
    par_pack(a0, n, lhs);
    par_pack(b0, n, rhs);
    memcpy(a1, a0, (size_t)n * sizeof(uint32_t));
    memcpy(b1, b0, (size_t)n * sizeof(uint32_t));

    if (n < PAR_STAGE_MIN_N) {
        /* 4 路：forward(a0/b0/a1/b1) → 点乘 → 逆变换 */
        core_fwd_job_ty fj[4] = {
            { a0, log2n, mods[0], NTT_OK_E },
            { b0, log2n, mods[0], NTT_OK_E },
            { a1, log2n, mods[1], NTT_OK_E },
            { b1, log2n, mods[1], NTT_OK_E }
        };
        nex_thread_pool_run(pool, 4U, core_fwd_fn, fj);
        for (size_t i = 0U; i < 4U; i++) {
            if (fj[i].err != NTT_OK_E) {
                err = BIGINT_ERR_INVALID_E;
                goto cleanup;
            }
        }
        core_pw_job_ty pj[2] = {
            { a0, a0, b0, n, mods[0], NTT_OK_E },
            { a1, a1, b1, n, mods[1], NTT_OK_E }
        };
        nex_thread_pool_run(pool, 2U, core_pw_fn, pj);
        for (size_t i = 0U; i < 2U; i++) {
            if (pj[i].err != NTT_OK_E) {
                err = BIGINT_ERR_INVALID_E;
                goto cleanup;
            }
        }
        core_inv_job_ty ij[2] = {
            { a0, log2n, mods[0], NTT_OK_E },
            { a1, log2n, mods[1], NTT_OK_E }
        };
        nex_thread_pool_run(pool, 2U, core_inv_fn, ij);
        for (size_t i = 0U; i < 2U; i++) {
            if (ij[i].err != NTT_OK_E) {
                err = BIGINT_ERR_INVALID_E;
                goto cleanup;
            }
        }
    } else {
        /* 大变换：扁平任务图——每个 stage 只做一次 barrier，同时处理
         * a0/b0/a1/b1 四个缓冲区；点乘与逆变换同理。 */
        nex_par_ntt_buf_ty fwd[4] = {
            { a0, mods[0] }, { b0, mods[0] },
            { a1, mods[1] }, { b1, mods[1] }
        };
        nex_par_ntt_forward_multi(fwd, 4U, log2n, pool);

        nex_par_ntt_pw_ty pw[2] = {
            { a0, a0, b0, mods[0] },
            { a1, a1, b1, mods[1] }
        };
        nex_par_ntt_pointwise_multi(pw, 2U, n, pool);

        nex_par_ntt_buf_ty inv[2] = {
            { a0, mods[0] }, { a1, mods[1] }
        };
        nex_par_ntt_inverse_multi(inv, 2U, log2n, pool);
    }

    /* Garner CRT：系数重构按 k 并行；进位按 4 系数一块并行归约 +
     * 块间前缀串行（串行迭代数降为 K/4）。 */
    {
        const size_t kcoeff = 2U * s;
        const size_t crt_len = (kcoeff + 3U) & ~(size_t)3U;
        const size_t ncoeff = kcoeff - 1U;
        uint64_t *coeff = (uint64_t *)calloc(crt_len, sizeof(uint64_t));
        if (coeff == NULL) {
            err = BIGINT_ERR_OOM_E;
            goto cleanup;
        }
        const uint32_t p2 = mods[1]->p;
        const uint32_t p1_inv = ntt_mod_inv(
                (uint32_t)((uint64_t)mods[0]->p % p2), mods[1]);
        unsigned nt = (pool != NULL) ? nex_thread_pool_count(pool) : 1U;
        if (nt == 0U) {
            nt = 1U;
        }
        if (nt > 64U) {
            nt = 64U;
        }
        if ((size_t)nt > ncoeff) {
            nt = (unsigned)ncoeff;
        }
        crt_job_ty jobs[64];
        for (unsigned t = 0U; t < nt; t++) {
            jobs[t].r1 = a0;
            jobs[t].r2 = a1;
            jobs[t].coeff = coeff;
            jobs[t].p1 = mods[0]->p;
            jobs[t].p2 = p2;
            jobs[t].p1_inv = p1_inv;
            jobs[t].begin = ncoeff * (size_t)t / (size_t)nt;
            jobs[t].end = ncoeff * (size_t)(t + 1U) / (size_t)nt;
        }
        nex_thread_pool_run(pool, nt, crt_job_fn, jobs);

        const size_t nblocks = crt_len / 4U;
        uint64_t *lh = (uint64_t *)malloc(2U * nblocks * sizeof(uint64_t));
        if (lh == NULL) {
            free(coeff);
            err = BIGINT_ERR_OOM_E;
            goto cleanup;
        }
        uint64_t *low = lh;
        uint64_t *high = lh + nblocks;
        nt = (pool != NULL) ? nex_thread_pool_count(pool) : 1U;
        if (nt == 0U) {
            nt = 1U;
        }
        if (nt > 64U) {
            nt = 64U;
        }
        if ((size_t)nt > nblocks) {
            nt = (unsigned)nblocks;
        }
        blk_job_ty bj[64];
        for (unsigned t = 0U; t < nt; t++) {
            bj[t].coeff = coeff;
            bj[t].low = low;
            bj[t].high = high;
            bj[t].b0 = nblocks * (size_t)t / (size_t)nt;
            bj[t].b1 = nblocks * (size_t)(t + 1U) / (size_t)nt;
        }
        nex_thread_pool_run(pool, nt, blk_job_fn, bj);

        uint64_t carry = 0U;
        for (size_t b = 0U; b < nblocks; b++) {
            const uint64_t sum = low[b] + carry;
            const uint64_t cbit = (sum < low[b]) ? 1U : 0U;
            carry = high[b] + cbit;
            for (unsigned j = 0U; j < 4U; j++) {
                const size_t k = 4U * b + j;
                if (k < kcoeff) {
                    const uint32_t digit = (uint32_t)((sum >> (16U * j))
                            & 0xFFFFU);
                    if ((k & 1U) == 0U) {
                        out.limbs[k >> 1U] = digit;
                    } else {
                        out.limbs[k >> 1U] |= digit << 16U;
                    }
                }
            }
        }
        free(lh);
        free(coeff);
        out.len = s;
        par_normalize(&out);
        if (out.len > 0U) {
            const bool neg = (lhs->sign == BIGINT_SIGN_NEG_E)
                    != (rhs->sign == BIGINT_SIGN_NEG_E);
            out.sign = neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
        }
        bigint_bin_move(dst, &out);
    }

cleanup:
    bigint_bin_free(&out);
    free(a1);
    free(b1);
    free(a0);
    free(b0);
    return err;
}
