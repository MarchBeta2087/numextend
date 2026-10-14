#include "nex/series/nex_series.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/convert/nex_convert.h"

/*
 * 路线 A：精确系数 + 全局尾界（设计文档 §15.6，TODO §C2）。
 * 具体把二项式级数 (1+z)^alpha 的 (A,B) 模型接到自适应截断与数值求值。
 */

/* r = |f| */
static bigfrac_err_ty frac_abs(bigfrac_ty *out, const bigfrac_ty *f) {
    bigfrac_ty zero;
    bigfrac_init(&zero);
    bigfrac_err_ty err = bigfrac_copy(out, f);
    if (err == BIGFRAC_OK_E && bigfrac_cmp(f, &zero) < 0) {
        err = bigfrac_neg(out);
    }
    bigfrac_free(&zero);
    return err;
}

static bigfrac_err_ty frac_from_i64(bigfrac_ty *out, int64_t v) {
    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_init(&num);
    bigint_bin_init(&den);
    bigint_bin_from_i64(&num, v);
    bigint_bin_from_u64(&den, 1U);
    bigfrac_err_ty err = bigfrac_from_ints(out, &num, &den);
    bigint_bin_free(&den);
    bigint_bin_free(&num);
    return err;
}

/* floor(f) -> int64（f 为有理数） */
static bigfrac_err_ty frac_floor_i64(const bigfrac_ty *f, int64_t *out) {
    bigint_bin_ty quot;
    bigint_bin_ty rem;
    bigint_bin_init(&quot);
    bigint_bin_init(&rem);
    bigfrac_err_ty err = BIGFRAC_OK_E;

    bigint_err_ty ierr = bigint_bin_div_rem(&quot, &rem, bigfrac_num(f),
                                            bigfrac_den(f));
    if (ierr != BIGINT_OK_E) {
        err = BIGFRAC_ERR_OOM_E;
    }
    int64_t q = 0;
    if (err == BIGFRAC_OK_E) {
        ierr = bigint_bin_to_i64(&quot, &q);
        if (ierr != BIGINT_OK_E) {
            err = BIGFRAC_ERR_INVALID_E;   /* 上指标过大 */
        }
    }
    if (err == BIGFRAC_OK_E && bigint_bin_sign(bigfrac_num(f)) ==
            BIGINT_SIGN_NEG_E && !bigint_bin_is_zero(&rem)) {
        q -= 1;                            /* C 截断 -> 向下取整 */
    }
    if (err == BIGFRAC_OK_E) {
        *out = q;
    }

    bigint_bin_free(&rem);
    bigint_bin_free(&quot);
    return err;
}

bigfrac_err_ty nex_series_binom_model(bigfrac_ty *A, bigfrac_ty *B,
        const bigfrac_ty *alpha) {
    if (A == NULL || B == NULL || alpha == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty minus_one;
    bigfrac_init(&minus_one);
    bigfrac_err_ty err = frac_from_i64(&minus_one, -1);
    if (err == BIGFRAC_OK_E && bigfrac_cmp(alpha, &minus_one) < 0) {
        err = BIGFRAC_ERR_UNSUPPORTED_E;   /* alpha < -1：系数无界 */
    }

    int64_t fl = 0;
    if (err == BIGFRAC_OK_E) {
        err = frac_floor_i64(alpha, &fl);
    }
    int64_t k_max = fl + 2;
    if (k_max < 2) {
        k_max = 2;
    }
    if (err == BIGFRAC_OK_E && k_max > 100000) {
        err = BIGFRAC_ERR_UNSUPPORTED_E;   /* 上指标过大，扫描不现实 */
    }

    bigfrac_ty c;
    bigfrac_ty mag;
    bigfrac_init(&c);
    bigfrac_init(&mag);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_from_str(A, "0", NULL);   /* A = 0 */
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_from_str(B, "1", NULL);   /* B = 1 */
    }
    for (int64_t k = 0; k <= k_max && err == BIGFRAC_OK_E; k++) {
        err = bigfrac_binom(&c, alpha, (size_t)k);
        if (err == BIGFRAC_OK_E) {
            err = frac_abs(&mag, &c);
        }
        if (err == BIGFRAC_OK_E && bigfrac_cmp(&mag, A) > 0) {
            err = bigfrac_copy(A, &mag);
        }
    }

    bigfrac_free(&mag);
    bigfrac_free(&c);
    bigfrac_free(&minus_one);
    return err;
}

bigfrac_err_ty nex_series_binom_order(size_t *out_n, const bigfrac_ty *alpha,
        const bigfrac_ty *r, const bigfrac_ty *eps) {
    if (out_n == NULL || alpha == NULL || r == NULL || eps == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    bigfrac_ty A;
    bigfrac_ty B;
    bigfrac_init(&A);
    bigfrac_init(&B);
    bigfrac_err_ty err = nex_series_binom_model(&A, &B, alpha);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_order_geometric(out_n, &A, &B, r, eps);
    }
    bigfrac_free(&B);
    bigfrac_free(&A);
    return err;
}

static bigfloat_err_ty map_frac_err(bigfrac_err_ty err) {
    if (err == BIGFRAC_ERR_OOM_E) {
        return BIGFLOAT_ERR_OOM_E;
    }
    return BIGFLOAT_ERR_INVALID_E;   /* INVALID / UNSUPPORTED 均归为 INVALID */
}

static bigdecimal_err_ty map_frac_err_dec(bigfrac_err_ty err) {
    if (err == BIGFRAC_ERR_OOM_E) {
        return BIGDECIMAL_ERR_OOM_E;
    }
    return BIGDECIMAL_ERR_INVALID_E;
}

bigfloat_err_ty nex_series_binom_eval_float(bigfloat_ty *out,
        const bigfrac_ty *alpha, const bigfloat_ty *z,
        const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx) {
    if (out == NULL || alpha == NULL || z == NULL || eps == NULL ||
            ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    bigfrac_ty zf;
    bigfrac_init(&zf);
    nex_convert_err_ty conv = nex_convert_float_to_frac(&zf, z);
    if (conv != NEX_CONVERT_OK_E) {
        bigfrac_free(&zf);
        return (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                               : BIGFLOAT_ERR_INVALID_E;
    }

    bigfrac_ty r;
    bigfrac_init(&r);
    bigfrac_err_ty err = frac_abs(&r, &zf);

    size_t n = 0U;
    if (err == BIGFRAC_OK_E) {
        err = nex_series_binom_order(&n, alpha, &r, eps);
    }

    bigfrac_series_ty s;
    bigfrac_series_init(&s);
    bigfrac_ty partial;
    bigfrac_init(&partial);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_binom(&s, alpha, n);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_eval(&partial, &s, &zf);
    }

    bigfloat_err_ty result = BIGFLOAT_OK_E;
    if (err != BIGFRAC_OK_E) {
        result = map_frac_err(err);
    } else {
        conv = nex_convert_frac_to_float(out, &partial, ctx);
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                                     : BIGFLOAT_ERR_INVALID_E;
        }
    }

    bigfrac_free(&partial);
    bigfrac_series_free(&s);
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}

bigdecimal_err_ty nex_series_binom_eval_decimal(bigdecimal_ty *out,
        const bigfrac_ty *alpha, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || alpha == NULL || z == NULL || eps == NULL ||
            ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    bigfrac_ty zf;
    bigfrac_init(&zf);
    nex_convert_err_ty conv = nex_convert_decimal_to_frac(&zf, z);
    if (conv != NEX_CONVERT_OK_E) {
        bigfrac_free(&zf);
        return (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                               : BIGDECIMAL_ERR_INVALID_E;
    }

    bigfrac_ty r;
    bigfrac_init(&r);
    bigfrac_err_ty err = frac_abs(&r, &zf);

    size_t n = 0U;
    if (err == BIGFRAC_OK_E) {
        err = nex_series_binom_order(&n, alpha, &r, eps);
    }

    bigfrac_series_ty s;
    bigfrac_series_init(&s);
    bigfrac_ty partial;
    bigfrac_init(&partial);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_binom(&s, alpha, n);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_eval(&partial, &s, &zf);
    }

    bigdecimal_err_ty result = BIGDECIMAL_OK_E;
    if (err != BIGFRAC_OK_E) {
        result = map_frac_err_dec(err);
    } else {
        conv = nex_convert_frac_to_decimal(out, &partial, ctx);
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                                     : BIGDECIMAL_ERR_INVALID_E;
        }
    }

    bigfrac_free(&partial);
    bigfrac_series_free(&s);
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}
