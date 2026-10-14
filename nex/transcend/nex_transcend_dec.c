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
