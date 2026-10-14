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
