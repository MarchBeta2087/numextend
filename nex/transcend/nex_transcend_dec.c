#include "nex/transcend/nex_transcend.h"

#include <stddef.h>

#include "nex/ball/nex_ball.h"
#include "nex/bigint/dec/nex_bigint_dec.h"

/* 将 x 舍入到目标 ctx（decompose + compose，避免零操作数捷径） */
static bigdecimal_err_ty bd_round_to_ctx(bigdecimal_ty *out,
        const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx) {
    bigint_dec_ty mant;
    bigint_dec_init(&mant);
    int64_t e = 0;
    bigdecimal_flag_ty fl = BIGDECIMAL_POS_ZERO_E;
    bigdecimal_err_ty err = bigdecimal_decompose(x, &mant, &e, &fl);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_compose(out, &mant, e, fl, ctx);
    }
    bigint_dec_free(&mant);
    return err;
}

/*
 * 超越函数（设计文档 §13 #8 / §D）——正确舍入的十进制 atan。
 *
 * 步骤（与 bigfloat 版同构）：
 *   1. 以球 [x, 0] 表示输入，在工作精度 p（初值 ctx->mant_digits + 16）下用
 *      `bigdecimal_ball_atan`（折半 + 级数 + 半径严格传播）得严谨球；
 *   2. 球端点按定向舍入取外边界，再舍入到目标 ctx；两端点相同则为正确舍入；
 *   3. 否则 p 翻倍重试（Ziv 策略）。
 */

bigdecimal_err_ty bigdecimal_atan(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(x) || bigdecimal_is_inf(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    bigdecimal_ball_ty xb;
    bigdecimal_ball_init(&xb);
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = bigdecimal_ball_set(&xb, x, &zero, ctx);

    bigdecimal_err_ty result = BIGDECIMAL_ERR_INVALID_E;
    size_t p = ctx->mant_digits + 16U;
    for (int iter = 0; iter < 10 && err == BIGDECIMAL_OK_E; iter++) {
        bigdecimal_ctx_ty wctx;
        bigdecimal_ctx_ty wdn;
        bigdecimal_ctx_ty wup;
        if (bigdecimal_ctx_make(&wctx, p, ctx->exp_digits,
                                BIGDECIMAL_ROUND_NEAREST_EVEN_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wdn, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_NEG_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wup, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_POS_E) !=
                    BIGDECIMAL_OK_E) {
            result = BIGDECIMAL_ERR_INVALID_E;
            break;
        }

        bigdecimal_ball_ty b;
        bigdecimal_ball_init(&b);
        err = bigdecimal_ball_atan(&b, &xb, &wctx);
        if (err != BIGDECIMAL_OK_E) {
            result = err;
            bigdecimal_ball_free(&b);
            break;
        }

        bigdecimal_ty lo;
        bigdecimal_ty hi;
        bigdecimal_ty Lo;
        bigdecimal_ty Hi;
        bigdecimal_init(&lo);
        bigdecimal_init(&hi);
        bigdecimal_init(&Lo);
        bigdecimal_init(&Hi);
        err = bigdecimal_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGDECIMAL_OK_E && bigdecimal_eq(&Lo, &Hi)) {
            err = bigdecimal_copy(out, &Lo);
            result = BIGDECIMAL_OK_E;
        }
        bigdecimal_free(&Hi);
        bigdecimal_free(&Lo);
        bigdecimal_free(&hi);
        bigdecimal_free(&lo);
        bigdecimal_ball_free(&b);
        if (result == BIGDECIMAL_OK_E || err != BIGDECIMAL_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigdecimal_free(&zero);
    bigdecimal_ball_free(&xb);
    return (err != BIGDECIMAL_OK_E) ? err : result;
}

/* atan(1) 的球 = π/4（atan 球严格） */
static bigdecimal_err_ty bd_atan1_ball(bigdecimal_ball_ty *out,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ball_ty one;
    bigdecimal_ball_init(&one);
    bigdecimal_ty o;
    bigdecimal_ty z;
    bigdecimal_init(&o);
    bigdecimal_init(&z);
    bigdecimal_err_ty err = bigdecimal_from_str(&o, "1", ctx, NULL);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_set(&one, &o, &z, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_atan(out, &one, ctx);
    }
    bigdecimal_free(&z);
    bigdecimal_free(&o);
    bigdecimal_ball_free(&one);
    return err;
}

/* out = ±v（neg 时仅翻转中心；半径不变） */
static bigdecimal_err_ty bd_ball_neg(bigdecimal_ball_ty *out,
        const bigdecimal_ball_ty *v, bool neg) {
    bigdecimal_err_ty err = bigdecimal_ball_copy(out, v);
    if (err == BIGDECIMAL_OK_E && neg) {
        err = bigdecimal_neg(&out->mid);
    }
    return err;
}

/* t ≥ 0 的球式 atan：t>1 时用 π/2 − atan(1/t)（hp = π/2 球） */
static bigdecimal_err_ty bd_atan_nonneg(bigdecimal_ball_ty *out,
        const bigdecimal_ball_ty *t, const bigdecimal_ball_ty *hp,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty one;
    bigdecimal_ty lo;
    bigdecimal_ty hi;
    bigdecimal_init(&one);
    bigdecimal_init(&lo);
    bigdecimal_init(&hi);
    (void)bigdecimal_from_str(&one, "1", ctx, NULL);
    (void)bigdecimal_sub(&lo, &t->mid, &t->rad, ctx);
    (void)bigdecimal_add(&hi, &t->mid, &t->rad, ctx);
    const bool gt1 = (bigdecimal_cmp(&lo, &one) > 0);
    bigdecimal_free(&hi);
    bigdecimal_free(&lo);
    bigdecimal_free(&one);
    if (!gt1) {
        return bigdecimal_ball_atan(out, t, ctx);
    }
    bigdecimal_ball_ty oneb;
    bigdecimal_ball_ty it;
    bigdecimal_ball_ty at;
    bigdecimal_ball_init(&oneb);
    bigdecimal_ball_init(&it);
    bigdecimal_ball_init(&at);
    bigdecimal_ty o;
    bigdecimal_ty z;
    bigdecimal_init(&o);
    bigdecimal_init(&z);
    bigdecimal_err_ty err = bigdecimal_from_str(&o, "1", ctx, NULL);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_set(&oneb, &o, &z, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_div(&it, &oneb, t, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_atan(&at, &it, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_sub(out, hp, &at, ctx);
    }
    bigdecimal_free(&z);
    bigdecimal_free(&o);
    bigdecimal_ball_free(&at);
    bigdecimal_ball_free(&it);
    bigdecimal_ball_free(&oneb);
    return err;
}

/* 精确输入 (y, x) 下 arg(x + iy) 的严谨球（直角分象限，无灾难抵消） */
static bigdecimal_err_ty bd_arg_ball(bigdecimal_ball_ty *out,
        const bigdecimal_ball_ty *y, const bigdecimal_ball_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    const bool yz = (bigdecimal_cmp(&y->mid, &zero) == 0);
    const bool xz = (bigdecimal_cmp(&x->mid, &zero) == 0);
    const bool xneg = (bigdecimal_cmp(&x->mid, &zero) < 0);
    const bool yneg = (bigdecimal_cmp(&y->mid, &zero) < 0);

    if (yz && !xneg) {
        err = bigdecimal_ball_set(out, &zero, &zero, ctx);
        bigdecimal_free(&zero);
        return err;
    }

    bigdecimal_ball_ty at1;
    bigdecimal_ball_ty hp;
    bigdecimal_ball_ty pi;
    bigdecimal_ball_init(&at1);
    bigdecimal_ball_init(&hp);
    bigdecimal_ball_init(&pi);
    err = bd_atan1_ball(&at1, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_add(&hp, &at1, &at1, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_add(&pi, &hp, &hp, ctx);
    }

    if (err == BIGDECIMAL_OK_E && yz) {
        err = bigdecimal_ball_copy(out, &pi);
    } else if (err == BIGDECIMAL_OK_E && xz) {
        err = bd_ball_neg(out, &hp, yneg);
    } else if (err == BIGDECIMAL_OK_E) {
        bigdecimal_ty ym;
        bigdecimal_ty xm;
        bigdecimal_init(&ym);
        bigdecimal_init(&xm);
        err = bigdecimal_copy(&ym, &y->mid);
        if (err == BIGDECIMAL_OK_E && yneg) {
            err = bigdecimal_neg(&ym);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_copy(&xm, &x->mid);
        }
        if (err == BIGDECIMAL_OK_E && xneg) {
            err = bigdecimal_neg(&xm);
        }
        bigdecimal_ball_ty yb;
        bigdecimal_ball_ty xb;
        bigdecimal_ball_ty tb;
        bigdecimal_ball_ty a;
        bigdecimal_ball_init(&yb);
        bigdecimal_ball_init(&xb);
        bigdecimal_ball_init(&tb);
        bigdecimal_ball_init(&a);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_set(&yb, &ym, &zero, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_set(&xb, &xm, &zero, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_ball_div(&tb, &yb, &xb, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_atan_nonneg(&a, &tb, &hp, ctx);
        }
        if (err == BIGDECIMAL_OK_E && xneg) {
            err = bigdecimal_ball_sub(&a, &pi, &a, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_ball_neg(out, &a, yneg);
        }
        bigdecimal_ball_free(&a);
        bigdecimal_ball_free(&tb);
        bigdecimal_ball_free(&xb);
        bigdecimal_ball_free(&yb);
        bigdecimal_free(&xm);
        bigdecimal_free(&ym);
    }

    bigdecimal_ball_free(&pi);
    bigdecimal_ball_free(&hp);
    bigdecimal_ball_free(&at1);
    bigdecimal_free(&zero);
    return err;
}

bigdecimal_err_ty bigdecimal_atan2(bigdecimal_ty *out, const bigdecimal_ty *y,
        const bigdecimal_ty *x, const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || y == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(y) || bigdecimal_is_inf(y) ||
            bigdecimal_is_nan(x) || bigdecimal_is_inf(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    bigdecimal_ball_ty xb;
    bigdecimal_ball_ty yb;
    bigdecimal_ball_init(&xb);
    bigdecimal_ball_init(&yb);
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = bigdecimal_ball_set(&xb, x, &zero, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_ball_set(&yb, y, &zero, ctx);
    }

    bigdecimal_err_ty result = BIGDECIMAL_ERR_INVALID_E;
    size_t p = ctx->mant_digits + 16U;
    for (int iter = 0; iter < 10 && err == BIGDECIMAL_OK_E; iter++) {
        bigdecimal_ctx_ty wctx;
        bigdecimal_ctx_ty wdn;
        bigdecimal_ctx_ty wup;
        if (bigdecimal_ctx_make(&wctx, p, ctx->exp_digits,
                                BIGDECIMAL_ROUND_NEAREST_EVEN_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wdn, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_NEG_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wup, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_POS_E) !=
                    BIGDECIMAL_OK_E) {
            result = BIGDECIMAL_ERR_INVALID_E;
            break;
        }

        bigdecimal_ball_ty b;
        bigdecimal_ball_init(&b);
        err = bd_arg_ball(&b, &yb, &xb, &wctx);
        if (err != BIGDECIMAL_OK_E) {
            result = err;
            bigdecimal_ball_free(&b);
            break;
        }

        bigdecimal_ty lo;
        bigdecimal_ty hi;
        bigdecimal_ty Lo;
        bigdecimal_ty Hi;
        bigdecimal_init(&lo);
        bigdecimal_init(&hi);
        bigdecimal_init(&Lo);
        bigdecimal_init(&Hi);
        err = bigdecimal_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGDECIMAL_OK_E && bigdecimal_eq(&Lo, &Hi)) {
            err = bigdecimal_copy(out, &Lo);
            result = BIGDECIMAL_OK_E;
        }
        bigdecimal_free(&Hi);
        bigdecimal_free(&Lo);
        bigdecimal_free(&hi);
        bigdecimal_free(&lo);
        bigdecimal_ball_free(&b);
        if (result == BIGDECIMAL_OK_E || err != BIGDECIMAL_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigdecimal_free(&zero);
    bigdecimal_ball_free(&yb);
    bigdecimal_ball_free(&xb);
    return (err != BIGDECIMAL_OK_E) ? err : result;
}

bigdecimal_err_ty bigdecimal_exp(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(x) || bigdecimal_is_inf(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    bigdecimal_ball_ty xb;
    bigdecimal_ball_init(&xb);
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = bigdecimal_ball_set(&xb, x, &zero, ctx);

    bigdecimal_err_ty result = BIGDECIMAL_ERR_INVALID_E;
    size_t p = ctx->mant_digits + 16U;
    for (int iter = 0; iter < 10 && err == BIGDECIMAL_OK_E; iter++) {
        bigdecimal_ctx_ty wctx;
        bigdecimal_ctx_ty wdn;
        bigdecimal_ctx_ty wup;
        if (bigdecimal_ctx_make(&wctx, p, ctx->exp_digits,
                                BIGDECIMAL_ROUND_NEAREST_EVEN_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wdn, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_NEG_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wup, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_POS_E) !=
                    BIGDECIMAL_OK_E) {
            result = BIGDECIMAL_ERR_INVALID_E;
            break;
        }

        bigdecimal_ball_ty b;
        bigdecimal_ball_init(&b);
        err = bigdecimal_ball_exp(&b, &xb, &wctx);
        if (err != BIGDECIMAL_OK_E) {
            result = err;
            bigdecimal_ball_free(&b);
            break;
        }

        bigdecimal_ty lo;
        bigdecimal_ty hi;
        bigdecimal_ty Lo;
        bigdecimal_ty Hi;
        bigdecimal_init(&lo);
        bigdecimal_init(&hi);
        bigdecimal_init(&Lo);
        bigdecimal_init(&Hi);
        err = bigdecimal_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGDECIMAL_OK_E && bigdecimal_eq(&Lo, &Hi)) {
            err = bigdecimal_copy(out, &Lo);
            result = BIGDECIMAL_OK_E;
        }
        bigdecimal_free(&Hi);
        bigdecimal_free(&Lo);
        bigdecimal_free(&hi);
        bigdecimal_free(&lo);
        bigdecimal_ball_free(&b);
        if (result == BIGDECIMAL_OK_E || err != BIGDECIMAL_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigdecimal_free(&zero);
    bigdecimal_ball_free(&xb);
    return (err != BIGDECIMAL_OK_E) ? err : result;
}

bigdecimal_err_ty bigdecimal_log(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(x) || bigdecimal_is_inf(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    if (bigdecimal_cmp(x, &zero) <= 0) {
        bigdecimal_free(&zero);
        return BIGDECIMAL_ERR_INVALID_E;
    }

    bigdecimal_ball_ty xb;
    bigdecimal_ball_init(&xb);
    bigdecimal_err_ty err = bigdecimal_ball_set(&xb, x, &zero, ctx);

    bigdecimal_err_ty result = BIGDECIMAL_ERR_INVALID_E;
    size_t p = ctx->mant_digits + 16U;
    for (int iter = 0; iter < 10 && err == BIGDECIMAL_OK_E; iter++) {
        bigdecimal_ctx_ty wctx;
        bigdecimal_ctx_ty wdn;
        bigdecimal_ctx_ty wup;
        if (bigdecimal_ctx_make(&wctx, p, ctx->exp_digits,
                                BIGDECIMAL_ROUND_NEAREST_EVEN_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wdn, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_NEG_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wup, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_POS_E) !=
                    BIGDECIMAL_OK_E) {
            result = BIGDECIMAL_ERR_INVALID_E;
            break;
        }

        bigdecimal_ball_ty b;
        bigdecimal_ball_init(&b);
        err = bigdecimal_ball_log(&b, &xb, &wctx);
        if (err != BIGDECIMAL_OK_E) {
            result = err;
            bigdecimal_ball_free(&b);
            break;
        }

        bigdecimal_ty lo;
        bigdecimal_ty hi;
        bigdecimal_ty Lo;
        bigdecimal_ty Hi;
        bigdecimal_init(&lo);
        bigdecimal_init(&hi);
        bigdecimal_init(&Lo);
        bigdecimal_init(&Hi);
        err = bigdecimal_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGDECIMAL_OK_E && bigdecimal_eq(&Lo, &Hi)) {
            err = bigdecimal_copy(out, &Lo);
            result = BIGDECIMAL_OK_E;
        }
        bigdecimal_free(&Hi);
        bigdecimal_free(&Lo);
        bigdecimal_free(&hi);
        bigdecimal_free(&lo);
        bigdecimal_ball_free(&b);
        if (result == BIGDECIMAL_OK_E || err != BIGDECIMAL_OK_E) {
            break;
        }
        p *= 2U;
    }

    bigdecimal_free(&zero);
    bigdecimal_ball_free(&xb);
    return (err != BIGDECIMAL_OK_E) ? err : result;
}

/* 一元球函数的 Ziv 正确舍入包装（sin/cos/tan 共用，十进制） */
typedef bigdecimal_err_ty (*bd_ball_unary_fn)(bigdecimal_ball_ty *,
        const bigdecimal_ball_ty *, const bigdecimal_ctx_ty *);

static bigdecimal_err_ty bd_ziv_unary(bigdecimal_ty *out,
        const bigdecimal_ty *x, bd_ball_unary_fn fn,
        const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(x) || bigdecimal_is_inf(x)) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ball_ty xb;
    bigdecimal_ball_init(&xb);
    bigdecimal_ty zero;
    bigdecimal_init(&zero);
    bigdecimal_err_ty err = bigdecimal_ball_set(&xb, x, &zero, ctx);
    bigdecimal_err_ty result = BIGDECIMAL_ERR_INVALID_E;
    size_t p = ctx->mant_digits + 16U;
    for (int iter = 0; iter < 10 && err == BIGDECIMAL_OK_E; iter++) {
        bigdecimal_ctx_ty wctx;
        bigdecimal_ctx_ty wdn;
        bigdecimal_ctx_ty wup;
        if (bigdecimal_ctx_make(&wctx, p, ctx->exp_digits,
                                BIGDECIMAL_ROUND_NEAREST_EVEN_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wdn, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_NEG_E) !=
                    BIGDECIMAL_OK_E ||
                bigdecimal_ctx_make(&wup, p, ctx->exp_digits,
                                    BIGDECIMAL_ROUND_TOWARD_POS_E) !=
                    BIGDECIMAL_OK_E) {
            result = BIGDECIMAL_ERR_INVALID_E;
            break;
        }
        bigdecimal_ball_ty b;
        bigdecimal_ball_init(&b);
        err = fn(&b, &xb, &wctx);
        if (err != BIGDECIMAL_OK_E) {
            result = err;
            bigdecimal_ball_free(&b);
            break;
        }
        bigdecimal_ty lo;
        bigdecimal_ty hi;
        bigdecimal_ty Lo;
        bigdecimal_ty Hi;
        bigdecimal_init(&lo);
        bigdecimal_init(&hi);
        bigdecimal_init(&Lo);
        bigdecimal_init(&Hi);
        err = bigdecimal_sub(&lo, &b.mid, &b.rad, &wdn);
        if (err == BIGDECIMAL_OK_E) {
            err = bigdecimal_add(&hi, &b.mid, &b.rad, &wup);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Lo, &lo, ctx);
        }
        if (err == BIGDECIMAL_OK_E) {
            err = bd_round_to_ctx(&Hi, &hi, ctx);
        }
        if (err == BIGDECIMAL_OK_E && bigdecimal_eq(&Lo, &Hi)) {
            err = bigdecimal_copy(out, &Lo);
            result = BIGDECIMAL_OK_E;
        }
        bigdecimal_free(&Hi);
        bigdecimal_free(&Lo);
        bigdecimal_free(&hi);
        bigdecimal_free(&lo);
        bigdecimal_ball_free(&b);
        if (result == BIGDECIMAL_OK_E || err != BIGDECIMAL_OK_E) {
            break;
        }
        p *= 2U;
    }
    bigdecimal_free(&zero);
    bigdecimal_ball_free(&xb);
    return (err != BIGDECIMAL_OK_E) ? err : result;
}

bigdecimal_err_ty bigdecimal_sin(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    return bd_ziv_unary(out, x, bigdecimal_ball_sin, ctx);
}

bigdecimal_err_ty bigdecimal_cos(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    return bd_ziv_unary(out, x, bigdecimal_ball_cos, ctx);
}

bigdecimal_err_ty bigdecimal_tan(bigdecimal_ty *out, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx) {
    return bd_ziv_unary(out, x, bigdecimal_ball_tan, ctx);
}
