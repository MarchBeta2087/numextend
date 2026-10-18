/*
 * nex_ball_pi.c：高精度 π 与 Payne–Hanek 实参规约（设计文档 §16.2）。
 *
 * π 由 Chudnovsky 公式经整数二分拆分（binary splitting）计算：
 *
 *   1/π = 12 · Σ_{k≥0} (−1)^k (6k)! (13591409 + 545140134k)
 *                        / ((3k)! (k!)^3 · 640320^{3k+3/2})
 *
 * 二分拆分的基项与合并式见 bs_base / bs_combine；每项约贡献 47 bit。
 *
 * Payne–Hanek：对 |x| = num/den，以 N 位定点 2/π 求
 *
 *   y = x · 2/π ≈ num·Q / (den·2^N)，  Q = floor(2/π · 2^N)
 *
 * 取 q = round(y)、r = (y − q)·π/2 ∈ [−π/4, π/4]，并把 q mod 4 作为象限。
 * r 以 R·2^{-K} 的整数形式输出，误差上界由三部分合成：
 *   - Q 截断：|Δy| ≤ |x|·2·2^{-N}（保守取 |x|·4·2^{-N}）；
 *   - π/2 截断：≤ 2^{-M-1}（M 为 π 的定点位数，|V|/D ≤ 1/2）；
 *   - 商截断：< 2^{-K}。
 *
 * 全程只使用 bigint_bin 整数运算，不依赖浮点，误差界可由调用方严格换算。
 */

#include "nex/ball/nex_ball_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Chudnovsky 每项贡献的二进制位数（实测 ≥ 47.1；留保守余量） */
#define NEX_PI_BITS_PER_TERM 47U

/* 640320^3 / 24，可放入 uint64 */
#define NEX_PI_C3_OVER_24 UINT64_C(10939058860032000)

/* a^3 不溢出 uint64 的最大 a（用于二分拆分基项） */
#define NEX_PI_MAX_TERM_INDEX UINT64_C(2642245)

typedef struct {
    bigint_bin_ty p;
    bigint_bin_ty q;
    bigint_bin_ty t;
} nex_bs_node_ty;

static void bs_node_init(nex_bs_node_ty *node) {
    bigint_bin_init(&node->p);
    bigint_bin_init(&node->q);
    bigint_bin_init(&node->t);
}

static void bs_node_free(nex_bs_node_ty *node) {
    bigint_bin_free(&node->t);
    bigint_bin_free(&node->q);
    bigint_bin_free(&node->p);
}

/* dst = src × v（v 为 uint64） */
static bigint_err_ty bi_mul_u64(bigint_bin_ty *dst,
        const bigint_bin_ty *src, uint64_t v) {
    bigint_bin_ty factor;
    bigint_bin_init(&factor);
    bigint_err_ty err = bigint_bin_from_u64(&factor, v);
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul(dst, src, &factor);
    }
    bigint_bin_free(&factor);
    return err;
}

/* 二分拆分基项 k（区间 [k, k+1)） */
static bigint_err_ty bs_base(nex_bs_node_ty *node, size_t k) {
    if (k == 0U) {
        bigint_err_ty err = bigint_bin_from_u64(&node->p, 1U);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_from_u64(&node->q, 1U);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_from_u64(&node->t, UINT64_C(13591409));
        }
        return err;
    }
    const uint64_t a = (uint64_t)k;
    if (a > NEX_PI_MAX_TERM_INDEX) {
        return BIGINT_ERR_INVALID_E;
    }
    bigint_err_ty err = bigint_bin_from_u64(&node->p, 6U * a - 5U);
    if (err == BIGINT_OK_E) {
        err = bi_mul_u64(&node->p, &node->p, 2U * a - 1U);
    }
    if (err == BIGINT_OK_E) {
        err = bi_mul_u64(&node->p, &node->p, 6U * a - 1U);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_from_u64(&node->q, a * a * a);
    }
    if (err == BIGINT_OK_E) {
        err = bi_mul_u64(&node->q, &node->q, NEX_PI_C3_OVER_24);
    }
    if (err == BIGINT_OK_E) {
        const uint64_t lin = UINT64_C(13591409)
                + UINT64_C(545140134) * a;
        err = bi_mul_u64(&node->t, &node->p, lin);
    }
    if (err == BIGINT_OK_E && (k & 1U) != 0U) {
        (void)bigint_bin_neg(&node->t);
    }
    return err;
}

/* 合并左右区间：P = P1·P2，Q = Q1·Q2，T = T1·Q2 + P1·T2 */
static bigint_err_ty bs_combine(nex_bs_node_ty *dst,
        const nex_bs_node_ty *a, const nex_bs_node_ty *b) {
    bigint_bin_ty p;
    bigint_bin_ty q;
    bigint_bin_ty t1;
    bigint_bin_ty t2;
    bigint_bin_init(&p);
    bigint_bin_init(&q);
    bigint_bin_init(&t1);
    bigint_bin_init(&t2);
    bigint_err_ty err = bigint_bin_mul(&p, &a->p, &b->p);
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul(&q, &a->q, &b->q);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul(&t1, &a->t, &b->q);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul(&t2, &a->p, &b->t);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&t1, &t1, &t2);
    }
    if (err == BIGINT_OK_E) {
        bigint_bin_free(&dst->p);
        dst->p = p;
        bigint_bin_init(&p);
        bigint_bin_free(&dst->q);
        dst->q = q;
        bigint_bin_init(&q);
        bigint_bin_free(&dst->t);
        dst->t = t1;
        bigint_bin_init(&t1);
    }
    bigint_bin_free(&t2);
    bigint_bin_free(&t1);
    bigint_bin_free(&q);
    bigint_bin_free(&p);
    return err;
}

/* 计算 [a, b) 的二分拆分节点 */
static bigint_err_ty bs_build(nex_bs_node_ty *node, size_t a, size_t b) {
    if (b - a == 1U) {
        return bs_base(node, a);
    }
    const size_t m = a + (b - a) / 2U;
    nex_bs_node_ty left;
    nex_bs_node_ty right;
    bs_node_init(&left);
    bs_node_init(&right);
    bigint_err_ty err = bs_build(&left, a, m);
    if (err == BIGINT_OK_E) {
        err = bs_build(&right, m, b);
    }
    if (err == BIGINT_OK_E) {
        err = bs_combine(node, &left, &right);
    }
    bs_node_free(&right);
    bs_node_free(&left);
    return err;
}

void nex_ball_ph_result_init(nex_ball_ph_result_ty *res) {
    if (res != NULL) {
        bigint_bin_init(&res->r);
        res->k = 0U;
        res->quad = 0U;
        res->neg = 0U;
        res->err_exp = 0;
    }
}

void nex_ball_ph_result_free(nex_ball_ph_result_ty *res) {
    if (res != NULL) {
        bigint_bin_free(&res->r);
    }
}

bigint_err_ty nex_ball_pi_fixed_internal(bigint_bin_ty *out, size_t bits) {
    if (out == NULL || bits == 0U || bits > NEX_BALL_PH_MAX_PI_BITS) {
        return BIGINT_ERR_INVALID_E;
    }
    const size_t guard = 96U;
    if (bits > NEX_BALL_PH_MAX_PI_BITS - guard) {
        return BIGINT_ERR_INVALID_E;
    }
    const size_t m = bits + guard;
    const size_t terms = (m + 200U) / NEX_PI_BITS_PER_TERM + 4U;

    nex_bs_node_ty node;
    bs_node_init(&node);
    bigint_err_ty err = bs_build(&node, 0U, terms);

    bigint_bin_ty sq_arg;
    bigint_bin_ty sq;
    bigint_bin_ty a;
    bigint_bin_ty den;
    bigint_bin_init(&sq_arg);
    bigint_bin_init(&sq);
    bigint_bin_init(&a);
    bigint_bin_init(&den);

    if (err == BIGINT_OK_E) {
        /* sqrt(10005 · 2^{2S})，S = m + 64（sqrt 与最终除法保护位） */
        const size_t s = m + 64U;
        err = bigint_bin_from_u64(&sq_arg, UINT64_C(10005));
        if (err == BIGINT_OK_E) {
            err = bigint_bin_shl(&sq_arg, &sq_arg, 2U * s);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_sqrt(&sq, &sq_arg);
        }
        if (err == BIGINT_OK_E) {
            /* A = Q · sqrt(10005)·2^S · 426880 ≈ π · T · 2^S */
            err = bigint_bin_mul(&a, &node.q, &sq);
        }
        if (err == BIGINT_OK_E) {
            err = bi_mul_u64(&a, &a, UINT64_C(426880));
        }
        if (err == BIGINT_OK_E) {
            /* π·2^bits = floor(A / (T · 2^{S−bits})) */
            err = bigint_bin_shl(&den, &node.t, s - bits);
        }
        if (err == BIGINT_OK_E) {
            (void)bigint_bin_abs(&den);
            err = bigint_bin_div_rem(out, NULL, &a, &den);
        }
    }

    bigint_bin_free(&den);
    bigint_bin_free(&a);
    bigint_bin_free(&sq);
    bigint_bin_free(&sq_arg);
    bs_node_free(&node);
    return err;
}

bigint_err_ty nex_ball_ph_reduce_internal(nex_ball_ph_result_ty *res,
        const bigint_bin_ty *num, const bigint_bin_ty *den, int sign,
        size_t pbits) {
    if (res == NULL || num == NULL || den == NULL
            || (sign != 1 && sign != -1) || pbits < 2U) {
        return BIGINT_ERR_INVALID_E;
    }
    const size_t bn = bigint_bin_bit_len(num);
    const size_t bd = bigint_bin_bit_len(den);
    if (bn == 0U || bd == 0U) {
        return BIGINT_ERR_INVALID_E;
    }
    /* |x| < 2^E，E 为 log2|x| 的整数上界 */
    const size_t ebound = (bn >= bd) ? (bn - bd + 1U) : 1U;
    if (ebound > NEX_BALL_PH_MAX_PI_BITS
            || pbits > NEX_BALL_PH_MAX_PI_BITS - ebound
            || ebound + pbits + 200U > NEX_BALL_PH_MAX_PI_BITS) {
        return BIGINT_ERR_INVALID_E;
    }

    const size_t nbits = ebound + pbits + 40U;  /* 2/π 定点位数 */
    const size_t mbits = nbits + 64U;           /* π 定点位数 */
    const size_t kbits = pbits + 48U;           /* r 的二进制小数位 */

    bigint_bin_ty pi;
    bigint_bin_ty two_pow;
    bigint_bin_ty qfix;
    bigint_bin_ty dscale;
    bigint_bin_ty prod;
    bigint_bin_ty q0;
    bigint_bin_ty rem;
    bigint_bin_ty qabs;
    bigint_bin_ty vabs;
    bigint_bin_ty p2;
    bigint_bin_ty w;
    bigint_bin_ty dbig;
    bigint_bin_ty r;
    unsigned out_quad = 0U;
    unsigned out_neg = 0U;
    bigint_bin_init(&pi);
    bigint_bin_init(&two_pow);
    bigint_bin_init(&qfix);
    bigint_bin_init(&dscale);
    bigint_bin_init(&prod);
    bigint_bin_init(&q0);
    bigint_bin_init(&rem);
    bigint_bin_init(&qabs);
    bigint_bin_init(&vabs);
    bigint_bin_init(&p2);
    bigint_bin_init(&w);
    bigint_bin_init(&dbig);
    bigint_bin_init(&r);

    bigint_err_ty err = nex_ball_pi_fixed_internal(&pi, mbits);
    if (err == BIGINT_OK_E) {
        /* Q = floor(2^{M+N+1} / π_M) ≈ 2/π · 2^N */
        err = bigint_bin_from_u64(&two_pow, 1U);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_shl(&two_pow, &two_pow, mbits + nbits + 1U);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_div_rem(&qfix, NULL, &two_pow, &pi);
        }
    }
    if (err == BIGINT_OK_E) {
        /* D = den·2^N；A = num·Q；q0 = floor(A/D) */
        err = bigint_bin_shl(&dscale, den, nbits);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_mul(&prod, num, &qfix);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_div_rem(&q0, &rem, &prod, &dscale);
        }
    }
    if (err == BIGINT_OK_E) {
        /* 就近取整：2·rem ≥ D 时 q = q0+1，V = rem − D < 0 */
        bigint_bin_ty twice_rem;
        bigint_bin_init(&twice_rem);
        err = bigint_bin_shl(&twice_rem, &rem, 1U);
        bool round_up = false;
        if (err == BIGINT_OK_E) {
            round_up = (bigint_bin_cmp(&twice_rem, &dscale) >= 0);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_copy(&qabs, &q0);
        }
        if (err == BIGINT_OK_E && round_up) {
            bigint_bin_ty one;
            bigint_bin_init(&one);
            (void)bigint_bin_from_u64(&one, 1U);
            err = bigint_bin_add(&qabs, &qabs, &one);
            bigint_bin_free(&one);
            if (err == BIGINT_OK_E) {
                err = bigint_bin_sub(&vabs, &dscale, &rem);
            }
        } else if (err == BIGINT_OK_E) {
            err = bigint_bin_copy(&vabs, &rem);
        }
        bigint_bin_free(&twice_rem);
        if (err == BIGINT_OK_E) {
            /* r 的符号：向上取整时 V < 0；x < 0 再翻转一次 */
            const bool vneg = round_up;
            out_neg = ((vneg != (sign < 0)) ? 1U : 0U);
            unsigned quad = 0U;
            if (bigint_bin_bit_test(&qabs, 0U)) {
                quad |= 1U;
            }
            if (bigint_bin_bit_test(&qabs, 1U)) {
                quad |= 2U;
            }
            if (sign < 0) {
                quad = (4U - quad) & 3U;
            }
            out_quad = quad;
        }
    }
    if (err == BIGINT_OK_E) {
        /* R = floor(|V|·P2·2^K / (D·2^M))，P2 = floor(π/2·2^M) */
        err = bigint_bin_shr(&p2, &pi, 1U);
        if (err == BIGINT_OK_E) {
            err = bigint_bin_mul(&w, &vabs, &p2);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_shl(&w, &w, kbits);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_shl(&dbig, &dscale, mbits);
        }
        if (err == BIGINT_OK_E) {
            err = bigint_bin_div_rem(&r, NULL, &w, &dbig);
        }
    }
    if (err == BIGINT_OK_E) {
        bigint_bin_free(&res->r);
        res->r = r;
        bigint_bin_init(&r);
        res->k = kbits;
        res->quad = out_quad;
        res->neg = out_neg;
        /* 误差指数：Q 截断、π/2 截断、商截断三者取最大再留 2 bit */
        const int64_t err_q = (int64_t)ebound - (int64_t)nbits + 4;
        const int64_t err_p = -(int64_t)mbits - 1;
        const int64_t err_k = -(int64_t)kbits;
        int64_t emax = (err_q > err_p) ? err_q : err_p;
        if (err_k > emax) {
            emax = err_k;
        }
        res->err_exp = emax + 2;
    }

    bigint_bin_free(&r);
    bigint_bin_free(&dbig);
    bigint_bin_free(&w);
    bigint_bin_free(&p2);
    bigint_bin_free(&vabs);
    bigint_bin_free(&qabs);
    bigint_bin_free(&rem);
    bigint_bin_free(&q0);
    bigint_bin_free(&prod);
    bigint_bin_free(&dscale);
    bigint_bin_free(&qfix);
    bigint_bin_free(&two_pow);
    bigint_bin_free(&pi);
    return err;
}
