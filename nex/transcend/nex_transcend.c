#include "nex/transcend/nex_transcend.h"

#include <stddef.h>

#include "nex/ball/nex_ball.h"
#include "nex/bigint/bin/nex_bigint_bin.h"

/* 将 x 舍入到目标 ctx（decompose + compose，避免 add(x,0) 的零操作数捷径） */
static bigfloat_err_ty round_to_ctx(bigfloat_ty *out, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty mant;
    bigint_bin_init(&mant);
    int64_t e = 0;
    bigfloat_flag_ty fl = BIGFLOAT_POS_ZERO_E;
    bigfloat_err_ty err = bigfloat_decompose(x, &mant, &e, &fl);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_compose(out, &mant, e, fl, ctx);
    }
    bigint_bin_free(&mant);
    return err;
}

/*
 * 超越函数（设计文档 §13 #8 / §D）——正确舍入的 atan。
 *
 * 步骤：
 *   1. 以球 [x, 0] 表示输入，在**工作精度** p（初值 ctx->mant_bits + 32）下
 *      用 `bigfloat_ball_atan`（实参折半 + 级数 + 半径严格传播）得严谨球；
 *   2. 将球端点按**定向舍入**取到工作精度的外边界，再舍入到目标 ctx；
 *      若两端点舍入到同一值，则该值为正确舍入结果；
 *   3. 否则 p 翻倍重试（Ziv 策略）。
 */

bigfloat_err_ty bigfloat_atan(bigfloat_ty *out, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx) {
    if (out == NULL || x == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(x) || bigfloat_is_inf(x)) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    bigfloat_ball_ty xb;
    bigfloat_ball_init(&xb);
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bigfloat_err_ty err = bigfloat_ball_set(&xb, x, &zero, ctx);

    bigfloat_err_ty result = BIGFLOAT_ERR_INVALID_E;
    size_t p = ctx->mant_bits + 32U;
    for (int iter = 0; iter < 10 && err == BIGFLOAT_OK_E; iter++) {
        bigfloat_ctx_ty wctx;
        bigfloat_ctx_ty wdn;
        bigfloat_ctx_ty wup;
        if (bigfloat_ctx_make(&wctx, p, ctx->exp_bits,
                              BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E ||
                bigfloat_ctx_make(&wdn, p, ctx->exp_bits,
                                  BIGFLOAT_ROUND_TOWARD_NEG_E) !=
                    BIGFLOAT_OK_E ||
                bigfloat_ctx_make(&wup, p, ctx->exp_bits,
                                  BIGFLOAT_ROUND_TOWARD_POS_E) !=
                    BIGFLOAT_OK_E) {
            result = BIGFLOAT_ERR_INVALID_E;
            break;
        }

        bigfloat_ball_ty b;
        bigfloat_ball_init(&b);
        err = bigfloat_ball_atan(&b, &xb, &wctx);
        if (err != BIGFLOAT_OK_E) {
            result = err;
            bigfloat_ball_free(&b);
            break;
        }

        bigfloat_ty lo;
        bigfloat_ty hi;
        bigfloat_ty Lo;
        bigfloat_ty Hi;
        bigfloat_init(&lo);
        bigfloat_init(&hi);
        bigfloat_init(&Lo);
        bigfloat_init(&Hi);
        err = bigfloat_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGFLOAT_OK_E) {
            err = round_to_ctx(&Lo, &lo, ctx);   /* 舍入到 ctx */
        }
        if (err == BIGFLOAT_OK_E) {
            err = round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGFLOAT_OK_E && bigfloat_eq(&Lo, &Hi)) {
            err = bigfloat_copy(out, &Lo);
            result = BIGFLOAT_OK_E;
        }
        bigfloat_free(&Hi);
        bigfloat_free(&Lo);
        bigfloat_free(&hi);
        bigfloat_free(&lo);
        bigfloat_ball_free(&b);
        if (result == BIGFLOAT_OK_E || err != BIGFLOAT_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigfloat_free(&zero);
    bigfloat_ball_free(&xb);
    return (err != BIGFLOAT_OK_E) ? err : result;
}

/* atan(1) 的球 = π/4（atan 球严格） */
static bigfloat_err_ty bf_atan1_ball(bigfloat_ball_ty *out,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ball_ty one;
    bigfloat_ball_init(&one);
    bigfloat_ty o;
    bigfloat_ty z;
    bigfloat_init(&o);
    bigfloat_init(&z);
    (void)bigfloat_from_f64(&o, 1.0);
    bigfloat_err_ty err = bigfloat_ball_set(&one, &o, &z, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_atan(out, &one, ctx);
    }
    bigfloat_free(&z);
    bigfloat_free(&o);
    bigfloat_ball_free(&one);
    return err;
}

/* out = ±v（neg 时仅翻转中心；半径不变） */
static bigfloat_err_ty bf_ball_neg(bigfloat_ball_ty *out,
        const bigfloat_ball_ty *v, bool neg) {
    bigfloat_err_ty err = bigfloat_ball_copy(out, v);
    if (err == BIGFLOAT_OK_E && neg) {
        err = bigfloat_neg(&out->mid);
    }
    return err;
}

/* t ≥ 0 的球式 atan：t>1 时用 π/2 − atan(1/t)（hp = π/2 球） */
static bigfloat_err_ty bf_atan_nonneg(bigfloat_ball_ty *out,
        const bigfloat_ball_ty *t, const bigfloat_ball_ty *hp,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ty one;
    bigfloat_ty lo;
    bigfloat_ty hi;
    bigfloat_init(&one);
    bigfloat_init(&lo);
    bigfloat_init(&hi);
    (void)bigfloat_from_f64(&one, 1.0);
    (void)bigfloat_sub(&lo, &t->mid, &t->rad, ctx);
    (void)bigfloat_add(&hi, &t->mid, &t->rad, ctx);
    const bool gt1 = (bigfloat_cmp(&lo, &one) > 0);
    bigfloat_free(&hi);
    bigfloat_free(&lo);
    bigfloat_free(&one);
    if (!gt1) {
        return bigfloat_ball_atan(out, t, ctx);
    }
    bigfloat_ball_ty oneb;
    bigfloat_ball_ty it;
    bigfloat_ball_ty at;
    bigfloat_ball_init(&oneb);
    bigfloat_ball_init(&it);
    bigfloat_ball_init(&at);
    bigfloat_ty o;
    bigfloat_ty z;
    bigfloat_init(&o);
    bigfloat_init(&z);
    (void)bigfloat_from_f64(&o, 1.0);
    bigfloat_err_ty err = bigfloat_ball_set(&oneb, &o, &z, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_div(&it, &oneb, t, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_atan(&at, &it, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_sub(out, hp, &at, ctx);
    }
    bigfloat_free(&z);
    bigfloat_free(&o);
    bigfloat_ball_free(&at);
    bigfloat_ball_free(&it);
    bigfloat_ball_free(&oneb);
    return err;
}

/* 精确输入 (y, x) 下 arg(x + iy) 的严谨球（直角分象限，无灾难抵消） */
static bigfloat_err_ty bf_arg_ball(bigfloat_ball_ty *out,
        const bigfloat_ball_ty *y, const bigfloat_ball_ty *x,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_err_ty err = BIGFLOAT_OK_E;
    bigfloat_ty zero;
    bigfloat_init(&zero);
    const bool yz = (bigfloat_cmp(&y->mid, &zero) == 0);
    const bool xz = (bigfloat_cmp(&x->mid, &zero) == 0);
    const bool xneg = (bigfloat_cmp(&x->mid, &zero) < 0);
    const bool yneg = (bigfloat_cmp(&y->mid, &zero) < 0);

    if (yz && !xneg) {   /* 正实轴含原点 → 0 */
        err = bigfloat_ball_set(out, &zero, &zero, ctx);
        bigfloat_free(&zero);
        return err;
    }

    bigfloat_ball_ty at1;
    bigfloat_ball_ty hp;
    bigfloat_ball_ty pi;
    bigfloat_ball_init(&at1);
    bigfloat_ball_init(&hp);
    bigfloat_ball_init(&pi);
    err = bf_atan1_ball(&at1, ctx);
    /* hp = π/2 = 2·atan1；pi = π = 2·hp */
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_add(&hp, &at1, &at1, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_add(&pi, &hp, &hp, ctx);
    }

    if (err == BIGFLOAT_OK_E && yz) {           /* 负实轴 → +π */
        err = bigfloat_ball_copy(out, &pi);
    } else if (err == BIGFLOAT_OK_E && xz) {    /* 虚轴 → sign(y)·π/2 */
        err = bf_ball_neg(out, &hp, yneg);
    } else if (err == BIGFLOAT_OK_E) {
        bigfloat_ty ym;
        bigfloat_ty xm;
        bigfloat_init(&ym);
        bigfloat_init(&xm);
        err = bigfloat_copy(&ym, &y->mid);
        if (err == BIGFLOAT_OK_E && yneg) {
            err = bigfloat_neg(&ym);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_copy(&xm, &x->mid);
        }
        if (err == BIGFLOAT_OK_E && xneg) {
            err = bigfloat_neg(&xm);
        }
        bigfloat_ball_ty yb;
        bigfloat_ball_ty xb;
        bigfloat_ball_ty tb;
        bigfloat_ball_ty a;
        bigfloat_ball_init(&yb);
        bigfloat_ball_init(&xb);
        bigfloat_ball_init(&tb);
        bigfloat_ball_init(&a);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_set(&yb, &ym, &zero, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_set(&xb, &xm, &zero, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_ball_div(&tb, &yb, &xb, ctx);   /* t = |y|/|x| ≥ 0 */
        }
        if (err == BIGFLOAT_OK_E) {
            err = bf_atan_nonneg(&a, &tb, &hp, ctx);
        }
        if (err == BIGFLOAT_OK_E && xneg) {
            err = bigfloat_ball_sub(&a, &pi, &a, ctx);     /* π − a */
        }
        if (err == BIGFLOAT_OK_E) {
            err = bf_ball_neg(out, &a, yneg);              /* sign(y)·a */
        }
        bigfloat_ball_free(&a);
        bigfloat_ball_free(&tb);
        bigfloat_ball_free(&xb);
        bigfloat_ball_free(&yb);
        bigfloat_free(&xm);
        bigfloat_free(&ym);
    }

    bigfloat_ball_free(&pi);
    bigfloat_ball_free(&hp);
    bigfloat_ball_free(&at1);
    bigfloat_free(&zero);
    return err;
}

bigfloat_err_ty bigfloat_atan2(bigfloat_ty *out, const bigfloat_ty *y,
        const bigfloat_ty *x, const bigfloat_ctx_ty *ctx) {
    if (out == NULL || y == NULL || x == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(y) || bigfloat_is_inf(y) ||
            bigfloat_is_nan(x) || bigfloat_is_inf(x)) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    bigfloat_ball_ty xb;
    bigfloat_ball_ty yb;
    bigfloat_ball_init(&xb);
    bigfloat_ball_init(&yb);
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bigfloat_err_ty err = bigfloat_ball_set(&xb, x, &zero, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ball_set(&yb, y, &zero, ctx);
    }

    bigfloat_err_ty result = BIGFLOAT_ERR_INVALID_E;
    size_t p = ctx->mant_bits + 32U;
    for (int iter = 0; iter < 10 && err == BIGFLOAT_OK_E; iter++) {
        bigfloat_ctx_ty wctx;
        bigfloat_ctx_ty wdn;
        bigfloat_ctx_ty wup;
        if (bigfloat_ctx_make(&wctx, p, ctx->exp_bits,
                              BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E ||
                bigfloat_ctx_make(&wdn, p, ctx->exp_bits,
                                  BIGFLOAT_ROUND_TOWARD_NEG_E) !=
                    BIGFLOAT_OK_E ||
                bigfloat_ctx_make(&wup, p, ctx->exp_bits,
                                  BIGFLOAT_ROUND_TOWARD_POS_E) !=
                    BIGFLOAT_OK_E) {
            result = BIGFLOAT_ERR_INVALID_E;
            break;
        }

        bigfloat_ball_ty b;
        bigfloat_ball_init(&b);
        err = bf_arg_ball(&b, &yb, &xb, &wctx);
        if (err != BIGFLOAT_OK_E) {
            result = err;
            bigfloat_ball_free(&b);
            break;
        }

        bigfloat_ty lo;
        bigfloat_ty hi;
        bigfloat_ty Lo;
        bigfloat_ty Hi;
        bigfloat_init(&lo);
        bigfloat_init(&hi);
        bigfloat_init(&Lo);
        bigfloat_init(&Hi);
        err = bigfloat_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGFLOAT_OK_E) {
            err = round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGFLOAT_OK_E && bigfloat_eq(&Lo, &Hi)) {
            err = bigfloat_copy(out, &Lo);
            result = BIGFLOAT_OK_E;
        }
        bigfloat_free(&Hi);
        bigfloat_free(&Lo);
        bigfloat_free(&hi);
        bigfloat_free(&lo);
        bigfloat_ball_free(&b);
        if (result == BIGFLOAT_OK_E || err != BIGFLOAT_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigfloat_free(&zero);
    bigfloat_ball_free(&yb);
    bigfloat_ball_free(&xb);
    return (err != BIGFLOAT_OK_E) ? err : result;
}

bigfloat_err_ty bigfloat_exp(bigfloat_ty *out, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx) {
    if (out == NULL || x == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(x) || bigfloat_is_inf(x)) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    bigfloat_ball_ty xb;
    bigfloat_ball_init(&xb);
    bigfloat_ty zero;
    bigfloat_init(&zero);
    bigfloat_err_ty err = bigfloat_ball_set(&xb, x, &zero, ctx);

    bigfloat_err_ty result = BIGFLOAT_ERR_INVALID_E;
    size_t p = ctx->mant_bits + 32U;
    for (int iter = 0; iter < 10 && err == BIGFLOAT_OK_E; iter++) {
        bigfloat_ctx_ty wctx;
        bigfloat_ctx_ty wdn;
        bigfloat_ctx_ty wup;
        if (bigfloat_ctx_make(&wctx, p, ctx->exp_bits,
                              BIGFLOAT_ROUND_NEAREST_EVEN_E) != BIGFLOAT_OK_E ||
                bigfloat_ctx_make(&wdn, p, ctx->exp_bits,
                                  BIGFLOAT_ROUND_TOWARD_NEG_E) !=
                    BIGFLOAT_OK_E ||
                bigfloat_ctx_make(&wup, p, ctx->exp_bits,
                                  BIGFLOAT_ROUND_TOWARD_POS_E) !=
                    BIGFLOAT_OK_E) {
            result = BIGFLOAT_ERR_INVALID_E;
            break;
        }

        bigfloat_ball_ty b;
        bigfloat_ball_init(&b);
        err = bigfloat_ball_exp(&b, &xb, &wctx);
        if (err != BIGFLOAT_OK_E) {
            result = err;
            bigfloat_ball_free(&b);
            break;
        }

        bigfloat_ty lo;
        bigfloat_ty hi;
        bigfloat_ty Lo;
        bigfloat_ty Hi;
        bigfloat_init(&lo);
        bigfloat_init(&hi);
        bigfloat_init(&Lo);
        bigfloat_init(&Hi);
        err = bigfloat_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGFLOAT_OK_E) {
            err = round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGFLOAT_OK_E) {
            err = round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGFLOAT_OK_E && bigfloat_eq(&Lo, &Hi)) {
            err = bigfloat_copy(out, &Lo);
            result = BIGFLOAT_OK_E;
        }
        bigfloat_free(&Hi);
        bigfloat_free(&Lo);
        bigfloat_free(&hi);
        bigfloat_free(&lo);
        bigfloat_ball_free(&b);
        if (result == BIGFLOAT_OK_E || err != BIGFLOAT_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigfloat_free(&zero);
    bigfloat_ball_free(&xb);
    return (err != BIGFLOAT_OK_E) ? err : result;
}
