#include "nex/ball/nex_ball.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/series/nex_series.h"

/*
 * bigdecimal 球式超越函数与实参规约（镜像 bigfloat 版，设计文档 §15.6）。
 * 算法同 bigfloat：折半（10 的幂非精确，用 ctx 舍入但半径覆盖）→ 路线 A
 * 级数初始球 → 球平方 / half-angle 倍角。为保守起见，缩放用 ctx 舍入并计入
 * 半径（decimal 下 2 的幂不保证精确）。
 */

static const size_t NEX_DBALL_HALVE_CAP = 20U;

static bigdecimal_err_ty ed_dir_ctx(bigdecimal_ctx_ty *out,
        const bigdecimal_ctx_ty *base, bigdecimal_round_ty round) {
    return bigdecimal_ctx_make(out, base->mant_digits, base->exp_digits,
                               round);
}

static bigdecimal_err_ty ed_abs(bigdecimal_ty *out, const bigdecimal_ty *x) {
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = bigdecimal_copy(out, x);
    if (err == BIGDECIMAL_OK_E && bigdecimal_cmp(x, &zero) < 0) {
        err = bigdecimal_neg(out);
    }
    bigdecimal_free(&zero);
    return err;
}

static bigdecimal_err_ty ed_ulp(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    bigint_dec_ty mant;
    bigint_dec_init(&mant);
    int64_t e = 0;
    bigdecimal_flag_ty fl = BIGDECIMAL_POS_ZERO_E;
    bigdecimal_err_ty err = bigdecimal_decompose(x, &mant, &e, &fl);
    if (err == BIGDECIMAL_OK_E) {
        if (fl == BIGDECIMAL_POS_E || fl == BIGDECIMAL_NEG_E) {
            bigint_dec_ty one;
            bigint_dec_init(&one);
            bigint_dec_from_u64(&one, 1U);
            err = bigdecimal_compose(out, &one,
                                     e - (int64_t)ctx->mant_digits,
                                     BIGDECIMAL_POS_E, ctx);
            bigint_dec_free(&one);
        } else {
            bigdecimal_ty zero;
            bigdecimal_init(&zero);
            err = bigdecimal_copy(out, &zero);
            bigdecimal_free(&zero);
        }
    }
    bigint_dec_free(&mant);
    return err;
}

static bigfrac_err_ty ed_frac_pow10_neg(bigfrac_ty *out, size_t m) {
    bigint_bin_ty one;
    bigint_bin_ty ten;
    bigint_bin_ty den;
    bigint_bin_init(&one);
    bigint_bin_init(&ten);
    bigint_bin_init(&den);
    bigint_bin_from_u64(&one, 1U);
    bigint_bin_from_u64(&ten, 10U);
    bigint_err_ty ierr = bigint_bin_copy(&den, &ten);
    for (size_t i = 1U; i < m && ierr == BIGINT_OK_E; i++) {
        ierr = bigint_bin_mul(&den, &den, &ten);
    }
    bigfrac_err_ty err;
    if (ierr != BIGINT_OK_E) {
        err = BIGFRAC_ERR_OOM_E;
    } else {
        err = bigfrac_from_ints(out, &one, &den);
    }
    bigint_bin_free(&den);
    bigint_bin_free(&ten);
    bigint_bin_free(&one);
    return err;
}

/* out = 2^k（k 可为负；decimal 下用乘/除，k ≤ 20 时精确） */
static bigdecimal_err_ty bd_pow2(bigdecimal_ty *out, int64_t k,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty two;
    bigdecimal_ty acc;
    bigdecimal_ty one;
    bigdecimal_init(&two);
    bigdecimal_init(&acc);
    bigdecimal_init(&one);
    bigdecimal_err_ty err = bigdecimal_from_str(&two, "2", ctx, NULL);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&one, "1", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&acc, &one);
    }
    int64_t m = (k < 0) ? -k : k;
    for (int64_t i = 0; i < m && err == BIGDECIMAL_OK_E; i++) {
        err = bigdecimal_mul(&acc, &acc, &two, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = (k >= 0) ? bigdecimal_copy(out, &acc)
                       : bigdecimal_div(out, &one, &acc, ctx);
    }
    bigdecimal_free(&one);
    bigdecimal_free(&acc);
    bigdecimal_free(&two);
    return err;
}

static bigdecimal_err_ty halvings_dec(size_t *out_k,
        const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = ed_dir_ctx(&up, ctx, BIGDECIMAL_ROUND_TOWARD_POS_E);
    bigdecimal_ty am;
    bigdecimal_ty M;
    bigdecimal_ty two;
    bigdecimal_ty half;
    bigdecimal_init(&am);
    bigdecimal_init(&M);
    bigdecimal_init(&two);
    bigdecimal_init(&half);
    if (err == BIGDECIMAL_OK_E) {
        err = ed_abs(&am, &x->mid);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&M, &am, &x->rad, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&two, "2", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&half, "0.5", ctx, NULL);
    }
    size_t k = 0U;
    while (err == BIGDECIMAL_OK_E && bigdecimal_cmp(&M, &half) > 0) {
        if (k >= NEX_DBALL_HALVE_CAP) {
            err = BIGDECIMAL_ERR_INVALID_E;
            break;
        }
        err = bigdecimal_div(&M, &M, &two, ctx);
        k++;
    }
    if (err == BIGDECIMAL_OK_E) {
        *out_k = k;
    }
    bigdecimal_free(&half);
    bigdecimal_free(&two);
    bigdecimal_free(&M);
    bigdecimal_free(&am);
    return err;
}

static bigdecimal_err_ty make_eval_ball_dec(bigdecimal_ball_ty *dst,
        const bigdecimal_ty *P, const bigdecimal_ty *eps,
        const bigdecimal_ty *inflate, const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = ed_dir_ctx(&up, ctx, BIGDECIMAL_ROUND_TOWARD_POS_E);
    bigdecimal_ty u1;
    bigdecimal_ty u2;
    bigdecimal_ty rad;
    bigdecimal_ty zero;
    bigdecimal_init(&u1);
    bigdecimal_init(&u2);
    bigdecimal_init(&rad);
    bigdecimal_init(&zero);
    if (err == BIGDECIMAL_OK_E) {
        err = ed_ulp(&u1, P, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = ed_ulp(&u2, eps, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(&rad, eps);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&rad, &rad, &u1, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_add(&rad, &rad, &u2, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        if (inflate != NULL && bigdecimal_cmp(inflate, &zero) > 0) {
            err = bigdecimal_add(&rad, &rad, inflate, &up);
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_set(dst, P, &rad, ctx);
    }
    bigdecimal_free(&zero);
    bigdecimal_free(&rad);
    bigdecimal_free(&u2);
    bigdecimal_free(&u1);
    return err;
}

static bigdecimal_err_ty exp_inflate_dec(bigdecimal_ty *out,
        const bigdecimal_ty *P, const bigdecimal_ty *r,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = ed_dir_ctx(&up, ctx, BIGDECIMAL_ROUND_TOWARD_POS_E);
    bigdecimal_ty zero;
    bigdecimal_ty one;
    bigdecimal_ty denom;
    bigdecimal_ty factor;
    bigdecimal_ty ap;
    bigdecimal_init(&zero);
    bigdecimal_init(&one);
    bigdecimal_init(&denom);
    bigdecimal_init(&factor);
    bigdecimal_init(&ap);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&one, "1", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_sub(&denom, &one, r, ctx);
        if (err == BIGDECIMAL_OK_E && bigdecimal_cmp(&denom, &zero) <= 0) {
            err = BIGDECIMAL_ERR_INVALID_E;
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_div(&factor, r, &denom, &up);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = ed_abs(&ap, P);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_mul(out, &ap, &factor, &up);
    }
    bigdecimal_free(&ap);
    bigdecimal_free(&factor);
    bigdecimal_free(&denom);
    bigdecimal_free(&one);
    bigdecimal_free(&zero);
    return err;
}

static bool ed_finite(const bigdecimal_ty *v) {
    return !bigdecimal_is_nan(v) && !bigdecimal_is_inf(v);
}

static bool ed_ball_ok(const bigdecimal_ball_ty *b) {
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bool ok = b != NULL && ed_finite(&b->mid) && ed_finite(&b->rad) &&
              bigdecimal_cmp(&b->rad, &zero) >= 0;
    bigdecimal_free(&zero);
    return ok;
}

bigdecimal_err_ty bigdecimal_ball_exp(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || x == NULL || ctx == NULL || !ed_ball_ok(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    size_t k = 0U;
    bigdecimal_err_ty err = halvings_dec(&k, x, ctx);

    bigdecimal_ty inv2k;
    bigdecimal_ty a_mid;
    bigdecimal_ty a_rad;
    bigdecimal_init(&inv2k);
    bigdecimal_init(&a_mid);
    bigdecimal_init(&a_rad);
    if (err == BIGDECIMAL_OK_E) {
        err = bd_pow2(&inv2k, -(int64_t)k, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_mul(&a_mid, &x->mid, &inv2k, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_mul(&a_rad, &x->rad, &inv2k, ctx);
    }

    bigfrac_ty eps;
    bigfrac_init(&eps);
    if (err == BIGDECIMAL_OK_E) {
        bigfrac_err_ty ferr = ed_frac_pow10_neg(&eps, ctx->mant_digits + 10U);
        if (ferr != BIGFRAC_OK_E) {
            err = (ferr == BIGFRAC_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                              : BIGDECIMAL_ERR_INVALID_E;
        }
    }

    bigdecimal_ty P;
    bigdecimal_ty ebound;
    bigdecimal_ty inflate;
    bigdecimal_ty zero;
    bigdecimal_init(&P);
    bigdecimal_init(&ebound);
    bigdecimal_init(&inflate);
    bigdecimal_init(&zero);
    if (err == BIGDECIMAL_OK_E) {
        err = nex_series_exp_eval_decimal(&P, &ebound, &a_mid, &eps, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_from_str(&inflate, "0", ctx, NULL);
    }
    if (err == BIGDECIMAL_OK_E && bigdecimal_cmp(&a_rad, &zero) > 0) {
        err = exp_inflate_dec(&inflate, &P, &a_rad, ctx);
    }

    bigdecimal_ball_ty E;
    bigdecimal_ball_init(&E);
    if (err == BIGDECIMAL_OK_E) {
        err = make_eval_ball_dec(&E, &P, &ebound, &inflate, ctx);
    }
    for (size_t i = 0U; i < k && err == BIGDECIMAL_OK_E; i++) {
        err = bigdecimal_ball_mul(&E, &E, &E, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_copy(dst, &E);
    }

    bigdecimal_ball_free(&E);
    bigdecimal_free(&zero);
    bigdecimal_free(&inflate);
    bigdecimal_free(&ebound);
    bigdecimal_free(&P);
    bigfrac_free(&eps);
    bigdecimal_free(&a_rad);
    bigdecimal_free(&a_mid);
    bigdecimal_free(&inv2k);
    return err;
}

static bigdecimal_err_ty sincos_reduced_dec(bigdecimal_ball_ty *sball,
        bigdecimal_ball_ty *cball, const bigdecimal_ball_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    size_t k = 0U;
    bigdecimal_err_ty err = halvings_dec(&k, x, ctx);

    bigdecimal_ty inv2k;
    bigdecimal_ty a_mid;
    bigdecimal_ty a_rad;
    bigdecimal_init(&inv2k);
    bigdecimal_init(&a_mid);
    bigdecimal_init(&a_rad);
    if (err == BIGDECIMAL_OK_E) {
        err = bd_pow2(&inv2k, -(int64_t)k, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_mul(&a_mid, &x->mid, &inv2k, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_mul(&a_rad, &x->rad, &inv2k, ctx);
    }

    bigfrac_ty eps;
    bigfrac_init(&eps);
    if (err == BIGDECIMAL_OK_E) {
        bigfrac_err_ty ferr = ed_frac_pow10_neg(&eps, ctx->mant_digits + 10U);
        if (ferr != BIGFRAC_OK_E) {
            err = (ferr == BIGFRAC_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                              : BIGDECIMAL_ERR_INVALID_E;
        }
    }

    bigdecimal_ty S;
    bigdecimal_ty es;
    bigdecimal_ty C;
    bigdecimal_ty ec;
    bigdecimal_init(&S);
    bigdecimal_init(&es);
    bigdecimal_init(&C);
    bigdecimal_init(&ec);
    if (err == BIGDECIMAL_OK_E) {
        err = nex_series_sin_eval_decimal(&S, &es, &a_mid, &eps, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = nex_series_cos_eval_decimal(&C, &ec, &a_mid, &eps, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = make_eval_ball_dec(sball, &S, &es, &a_rad, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = make_eval_ball_dec(cball, &C, &ec, &a_rad, ctx);
    }

    bigdecimal_ball_ty one_ball;
    bigdecimal_ball_init(&one_ball);
    if (err == BIGDECIMAL_OK_E) {
        bigdecimal_ty one;
        bigdecimal_init(&one);
        err = bigdecimal_from_str(&one, "1", ctx, NULL);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_set(&one_ball, &one, &one, ctx);
        }
        bigdecimal_free(&one);
    }

    bigdecimal_ball_ty t;
    bigdecimal_ball_ty s2;
    bigdecimal_ball_init(&t);
    bigdecimal_ball_init(&s2);
    for (size_t i = 0U; i < k && err == BIGDECIMAL_OK_E; i++) {
        err = bigdecimal_ball_mul(&t, cball, cball, ctx);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_add(&t, &t, &t, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_sub(&t, &t, &one_ball, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_mul(&s2, sball, cball, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_add(&s2, &s2, &s2, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_copy(cball, &t);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_copy(sball, &s2);
        }
    }

    bigdecimal_ball_free(&s2);
    bigdecimal_ball_free(&t);
    bigdecimal_ball_free(&one_ball);
    bigdecimal_free(&ec);
    bigdecimal_free(&C);
    bigdecimal_free(&es);
    bigdecimal_free(&S);
    bigfrac_free(&eps);
    bigdecimal_free(&a_rad);
    bigdecimal_free(&a_mid);
    bigdecimal_free(&inv2k);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_sin(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || x == NULL || ctx == NULL || !ed_ball_ok(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_ty s;
    bigdecimal_ball_ty c;
    bigdecimal_ball_init(&s);
    bigdecimal_ball_init(&c);
    bigdecimal_err_ty err = sincos_reduced_dec(&s, &c, x, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_copy(dst, &s);
    }
    bigdecimal_ball_free(&c);
    bigdecimal_ball_free(&s);
    return err;
}

bigdecimal_err_ty bigdecimal_ball_cos(bigdecimal_ball_ty *dst,
        const bigdecimal_ball_ty *x, const bigdecimal_ctx_ty *ctx) {
    if (dst == NULL || x == NULL || ctx == NULL || !ed_ball_ok(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_ty s;
    bigdecimal_ball_ty c;
    bigdecimal_ball_init(&s);
    bigdecimal_ball_init(&c);
    bigdecimal_err_ty err = sincos_reduced_dec(&s, &c, x, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_copy(dst, &c);
    }
    bigdecimal_ball_free(&c);
    bigdecimal_ball_free(&s);
    return err;
}
