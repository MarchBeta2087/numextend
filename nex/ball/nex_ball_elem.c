#include "nex/ball/nex_ball.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/series/nex_series.h"

/*
 * 球式超越函数与实参规约（路线 B 第二阶段，设计文档 §15.6）。
 *
 *   exp(x)：k 次折半使 |x|/2^k ≤ 1/2（2 的幂精确缩放）→ 路线 A 级数得初始球
 *           → k 次球平方回放；
 *   sin/cos：同样折半后由级数得初始球 → k 次 half-angle 倍角
 *           （cos2θ = 2cos²θ − 1，sin2θ = 2 sinθ cosθ）。
 * 半径全程由球运算严格传播；初始球半径含路线 A 尾界与舍入余量，并按输入
 * 半径用 Lipschitz/相对界膨胀。|x| 过大（k > 20）返回 INVALID。
 */

static const size_t NEX_BALL_HALVE_CAP = 20U;

static bigfloat_err_ty e_dir_ctx(bigfloat_ctx_ty *out,
        const bigfloat_ctx_ty *base, bigfloat_round_ty round) {
    return bigfloat_ctx_make(out, base->mant_bits, base->exp_bits, round);
}

static bigfloat_err_ty e_abs(bigfloat_ty *out, const bigfloat_ty *x) {
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bigfloat_err_ty err = bigfloat_copy(out, x);
    if (err == BIGFLOAT_OK_E && bigfloat_cmp(x, &zero) < 0) {
        err = bigfloat_neg(out);
    }
    bigfloat_free(&zero);
    return err;
}

static bigfloat_err_ty e_ulp(bigfloat_ty *out, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty mant;
    bigint_bin_init(&mant);
    int64_t e = 0;
    bigfloat_flag_ty fl = BIGFLOAT_POS_ZERO_E;
    bigfloat_err_ty err = bigfloat_decompose(x, &mant, &e, &fl);
    if (err == BIGFLOAT_OK_E) {
        if (fl == BIGFLOAT_POS_E || fl == BIGFLOAT_NEG_E) {
            bigint_bin_ty one;
            bigint_bin_init(&one);
            bigint_bin_from_u64(&one, 1U);
            err = bigfloat_compose(out, &one, e - (int64_t)ctx->mant_bits,
                                   BIGFLOAT_POS_E, ctx);
            bigint_bin_free(&one);
        } else {
            err = bigfloat_from_f64(out, 0.0);
        }
    }
    bigint_bin_free(&mant);
    return err;
}

/* out = 2^{-m}（bigfrac） */
static bigfrac_err_ty frac_pow2_neg(bigfrac_ty *out, size_t m) {
    bigint_bin_ty one;
    bigint_bin_ty den;
    bigint_bin_init(&one);
    bigint_bin_init(&den);
    bigint_bin_from_u64(&one, 1U);
    bigint_err_ty ierr = bigint_bin_shl(&den, &one, m);
    bigfrac_err_ty err;
    if (ierr != BIGINT_OK_E) {
        err = BIGFRAC_ERR_OOM_E;
    } else {
        err = bigfrac_from_ints(out, &one, &den);
    }
    bigint_bin_free(&den);
    bigint_bin_free(&one);
    return err;
}

/* out = 2^k（k 可为负） */
static bigfloat_err_ty bf_pow2(bigfloat_ty *out, int64_t k,
        const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty one;
    bigint_bin_init(&one);
    bigint_bin_from_u64(&one, 1U);
    bigfloat_err_ty err = bigfloat_compose(out, &one, k, BIGFLOAT_POS_E, ctx);
    bigint_bin_free(&one);
    return err;
}

/* 折半次数 k：使 |x|/2^k ≤ 1/2（M = |mid| + rad 为上界） */
static bigfloat_err_ty halvings(size_t *out_k, const bigfloat_ball_ty *x,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = e_dir_ctx(&up, ctx, BIGFLOAT_ROUND_TOWARD_POS_E);
    bigfloat_ty am;
    bigfloat_ty M;
    bigfloat_ty two;
    bigfloat_ty half;
    bigfloat_init(&am);
    bigfloat_init(&M);
    bigfloat_init(&two);
    bigfloat_init(&half);
    if (err == BIGFLOAT_OK_E) {
        err = e_abs(&am, &x->mid);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&M, &am, &x->rad, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&two, 2.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&half, 0.5);
    }
    size_t k = 0U;
    while (err == BIGFLOAT_OK_E && bigfloat_cmp(&M, &half) > 0) {
        if (k >= NEX_BALL_HALVE_CAP) {
            err = BIGFLOAT_ERR_INVALID_E;   /* |x| 过大 */
            break;
        }
        err = bigfloat_div(&M, &M, &two, ctx);
        k++;
    }
    if (err == BIGFLOAT_OK_E) {
        *out_k = k;
    }
    bigfloat_free(&half);
    bigfloat_free(&two);
    bigfloat_free(&M);
    bigfloat_free(&am);
    return err;
}

/* 由路线 A 结果 P（含尾界 eps）与额外膨胀 inflate 构造球 */
static bigfloat_err_ty make_eval_ball(bigfloat_ball_ty *dst,
        const bigfloat_ty *P, const bigfloat_ty *eps,
        const bigfloat_ty *inflate, const bigfloat_ctx_ty *ctx) {
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = e_dir_ctx(&up, ctx, BIGFLOAT_ROUND_TOWARD_POS_E);
    bigfloat_ty u1;
    bigfloat_ty u2;
    bigfloat_ty rad;
    bigfloat_ty zero;
    bigfloat_init(&u1);
    bigfloat_init(&u2);
    bigfloat_init(&rad);
    bigfloat_init(&zero);
    if (err == BIGFLOAT_OK_E) {
        err = e_ulp(&u1, P, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = e_ulp(&u2, eps, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(&rad, eps);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&rad, &rad, &u1, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_add(&rad, &rad, &u2, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        if (inflate != NULL && bigfloat_cmp(inflate, &zero) > 0) {
            err = bigfloat_add(&rad, &rad, inflate, &up);
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_set(dst, P, &rad, ctx);
    }
    bigfloat_free(&zero);
    bigfloat_free(&rad);
    bigfloat_free(&u2);
    bigfloat_free(&u1);
    return err;
}

/* inflate = |P|·(r/(1−r))，度量 exp 在输入半径 r 上的相对变化（0≤r≤1/2） */
static bigfloat_err_ty exp_inflate(bigfloat_ty *out, const bigfloat_ty *P,
        const bigfloat_ty *r, const bigfloat_ctx_ty *ctx) {
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = e_dir_ctx(&up, ctx, BIGFLOAT_ROUND_TOWARD_POS_E);
    bigfloat_ty zero;
    bigfloat_ty one;
    bigfloat_ty denom;
    bigfloat_ty factor;
    bigfloat_ty ap;
    bigfloat_init(&zero);
    bigfloat_init(&one);
    bigfloat_init(&denom);
    bigfloat_init(&factor);
    bigfloat_init(&ap);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&one, 1.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&denom, &one, r, ctx);
        if (err == BIGFLOAT_OK_E && bigfloat_cmp(&denom, &zero) <= 0) {
            err = BIGFLOAT_ERR_INVALID_E;
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_div(&factor, r, &denom, &up);
    }
    if (err == BIGFLOAT_OK_E) {
        err = e_abs(&ap, P);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(out, &ap, &factor, &up);
    }
    bigfloat_free(&ap);
    bigfloat_free(&factor);
    bigfloat_free(&denom);
    bigfloat_free(&one);
    bigfloat_free(&zero);
    return err;
}

static bool e_finite(const bigfloat_ty *v) {
    return !bigfloat_is_nan(v) && !bigfloat_is_inf(v);
}

static bool e_ball_ok(const bigfloat_ball_ty *b) {
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bool ok = b != NULL && e_finite(&b->mid) && e_finite(&b->rad) &&
              bigfloat_cmp(&b->rad, &zero) >= 0;
    bigfloat_free(&zero);
    return ok;
}

bigfloat_err_ty bigfloat_ball_exp(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || x == NULL || ctx == NULL || !e_ball_ok(x)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    size_t k = 0U;
    bigfloat_err_ty err = halvings(&k, x, ctx);

    bigfloat_ty inv2k;
    bigfloat_ty a_mid;
    bigfloat_ty a_rad;
    bigfloat_init(&inv2k);
    bigfloat_init(&a_mid);
    bigfloat_init(&a_rad);
    if (err == BIGFLOAT_OK_E) {
        err = bf_pow2(&inv2k, -(int64_t)k, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(&a_mid, &x->mid, &inv2k, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(&a_rad, &x->rad, &inv2k, ctx);
    }

    bigfrac_ty eps;
    bigfrac_init(&eps);
    if (err == BIGFLOAT_OK_E) {
        bigfrac_err_ty ferr = frac_pow2_neg(&eps, ctx->mant_bits + 20U);
        if (ferr != BIGFRAC_OK_E) {
            err = (ferr == BIGFRAC_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                              : BIGFLOAT_ERR_INVALID_E;
        }
    }

    bigfloat_ty P;
    bigfloat_ty ebound;
    bigfloat_ty inflate;
    bigfloat_ty zero;
    bigfloat_init(&P);
    bigfloat_init(&ebound);
    bigfloat_init(&inflate);
    bigfloat_init(&zero);
    if (err == BIGFLOAT_OK_E) {
        err = nex_series_exp_eval_float(&P, &ebound, &a_mid, &eps, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&inflate, 0.0);
    }
    if (err == BIGFLOAT_OK_E && bigfloat_cmp(&a_rad, &zero) > 0) {
        err = exp_inflate(&inflate, &P, &a_rad, ctx);
    }

    bigfloat_ball_ty E;
    bigfloat_ball_init(&E);
    if (err == BIGFLOAT_OK_E) {
        err = make_eval_ball(&E, &P, &ebound, &inflate, ctx);
    }
    for (size_t i = 0U; i < k && err == BIGFLOAT_OK_E; i++) {
        err = bigfloat_ball_mul(&E, &E, &E, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_copy(dst, &E);
    }

    bigfloat_ball_free(&E);
    bigfloat_free(&zero);
    bigfloat_free(&inflate);
    bigfloat_free(&ebound);
    bigfloat_free(&P);
    bigfrac_free(&eps);
    bigfloat_free(&a_rad);
    bigfloat_free(&a_mid);
    bigfloat_free(&inv2k);
    return err;
}

/* 折半 + half-angle 倍角，同时得 sin/cos 球 */
static bigfloat_err_ty sincos_reduced(bigfloat_ball_ty *sball,
        bigfloat_ball_ty *cball, const bigfloat_ball_ty *x,
        const bigfloat_ctx_ty *ctx) {
    size_t k = 0U;
    bigfloat_err_ty err = halvings(&k, x, ctx);

    bigfloat_ty inv2k;
    bigfloat_ty a_mid;
    bigfloat_ty a_rad;
    bigfloat_init(&inv2k);
    bigfloat_init(&a_mid);
    bigfloat_init(&a_rad);
    if (err == BIGFLOAT_OK_E) {
        err = bf_pow2(&inv2k, -(int64_t)k, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(&a_mid, &x->mid, &inv2k, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_mul(&a_rad, &x->rad, &inv2k, ctx);
    }

    bigfrac_ty eps;
    bigfrac_init(&eps);
    if (err == BIGFLOAT_OK_E) {
        bigfrac_err_ty ferr = frac_pow2_neg(&eps, ctx->mant_bits + 20U);
        if (ferr != BIGFRAC_OK_E) {
            err = (ferr == BIGFRAC_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                              : BIGFLOAT_ERR_INVALID_E;
        }
    }

    bigfloat_ty S;
    bigfloat_ty es;
    bigfloat_ty C;
    bigfloat_ty ec;
    bigfloat_init(&S);
    bigfloat_init(&es);
    bigfloat_init(&C);
    bigfloat_init(&ec);
    if (err == BIGFLOAT_OK_E) {
        err = nex_series_sin_eval_float(&S, &es, &a_mid, &eps, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = nex_series_cos_eval_float(&C, &ec, &a_mid, &eps, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = make_eval_ball(sball, &S, &es, &a_rad, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = make_eval_ball(cball, &C, &ec, &a_rad, ctx);
    }

    /* one_ball = [1, 1] */
    bigfloat_ball_ty one_ball;
    bigfloat_ball_init(&one_ball);
    if (err == BIGFLOAT_OK_E) {
        bigfloat_ty one;
        bigfloat_init(&one);
        err = bigfloat_from_f64(&one, 1.0);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_set(&one_ball, &one, &one, ctx);
        }
        bigfloat_free(&one);
    }

    bigfloat_ball_ty t;
    bigfloat_ball_ty s2;
    bigfloat_ball_init(&t);
    bigfloat_ball_init(&s2);
    for (size_t i = 0U; i < k && err == BIGFLOAT_OK_E; i++) {
        /* t = 2 c^2 - 1 */
        err = bigfloat_ball_mul(&t, cball, cball, ctx);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_add(&t, &t, &t, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_sub(&t, &t, &one_ball, ctx);
        }
        /* s2 = 2 s c */
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_mul(&s2, sball, cball, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_add(&s2, &s2, &s2, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_copy(cball, &t);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_copy(sball, &s2);
        }
    }

    bigfloat_ball_free(&s2);
    bigfloat_ball_free(&t);
    bigfloat_ball_free(&one_ball);
    bigfloat_free(&ec);
    bigfloat_free(&C);
    bigfloat_free(&es);
    bigfloat_free(&S);
    bigfrac_free(&eps);
    bigfloat_free(&a_rad);
    bigfloat_free(&a_mid);
    bigfloat_free(&inv2k);
    return err;
}

bigfloat_err_ty bigfloat_ball_sin(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || x == NULL || ctx == NULL || !e_ball_ok(x)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ball_ty s;
    bigfloat_ball_ty c;
    bigfloat_ball_init(&s);
    bigfloat_ball_init(&c);
    bigfloat_err_ty err = sincos_reduced(&s, &c, x, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_copy(dst, &s);
    }
    bigfloat_ball_free(&c);
    bigfloat_ball_free(&s);
    return err;
}

bigfloat_err_ty bigfloat_ball_cos(bigfloat_ball_ty *dst,
        const bigfloat_ball_ty *x, const bigfloat_ctx_ty *ctx) {
    if (dst == NULL || x == NULL || ctx == NULL || !e_ball_ok(x)) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ball_ty s;
    bigfloat_ball_ty c;
    bigfloat_ball_init(&s);
    bigfloat_ball_init(&c);
    bigfloat_err_ty err = sincos_reduced(&s, &c, x, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_copy(dst, &c);
    }
    bigfloat_ball_free(&c);
    bigfloat_ball_free(&s);
    return err;
}
