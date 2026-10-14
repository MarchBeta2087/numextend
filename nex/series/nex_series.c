#include "nex/series/nex_series.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/convert/nex_convert.h"
#include "nex/nex_alloc.h"

/*
 * 路线 A：精确系数 + 尾界（设计文档 §15.6，TODO §C2）。
 *   - 二项式级数：几何型全局尾界 A·q^n/(1−q)；
 *   - exp / sin / cos：阶乘型系数，用比值型尾界 t_n/(1−q_n)。
 * 求值以精确 bigfrac 累加，最后一次舍入到目标数值类型，可输出截断尾上界。
 */

static const size_t NEX_SERIES_ORDER_CAP = 1000000U;

static bigfrac_err_ty frac_u64(bigfrac_ty *out, uint64_t v) {
    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_init(&num);
    bigint_bin_init(&den);
    bigint_bin_from_u64(&num, v);
    bigint_bin_from_u64(&den, 1U);
    bigfrac_err_ty err = bigfrac_from_ints(out, &num, &den);
    bigint_bin_free(&den);
    bigint_bin_free(&num);
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

/* floor(f) -> int64 */
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
            err = BIGFRAC_ERR_INVALID_E;
        }
    }
    if (err == BIGFRAC_OK_E && bigint_bin_sign(bigfrac_num(f)) ==
            BIGINT_SIGN_NEG_E && !bigint_bin_is_zero(&rem)) {
        q -= 1;
    }
    if (err == BIGFRAC_OK_E) {
        *out = q;
    }

    bigint_bin_free(&rem);
    bigint_bin_free(&quot);
    return err;
}

/* ------------------------------------------------------------------ */
/* 二项式级数 (1+z)^alpha（几何型尾界）                                */
/* ------------------------------------------------------------------ */

bigfrac_err_ty nex_series_binom_model(bigfrac_ty *A, bigfrac_ty *B,
        const bigfrac_ty *alpha) {
    if (A == NULL || B == NULL || alpha == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty minus_one;
    bigfrac_init(&minus_one);
    bigfrac_err_ty err = frac_from_i64(&minus_one, -1);
    if (err == BIGFRAC_OK_E && bigfrac_cmp(alpha, &minus_one) < 0) {
        err = BIGFRAC_ERR_UNSUPPORTED_E;
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
        err = BIGFRAC_ERR_UNSUPPORTED_E;
    }

    bigfrac_ty c;
    bigfrac_ty mag;
    bigfrac_init(&c);
    bigfrac_init(&mag);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_from_str(A, "0", NULL);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_from_str(B, "1", NULL);
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

/* ------------------------------------------------------------------ */
/* 阶乘型级数 exp / cos / sin（比值型尾界）                            */
/*   t_j = z^{start+step*j} / (start+step*j)! · (−1)^{alt·j}           */
/*   exp: (0,1,0)   cos: (0,2,1)   sin: (1,2,1)                        */
/* ------------------------------------------------------------------ */

/* |t_n| 与 q_n = |t_{n+1}/t_n|，用于尾界 t_n/(1−q_n) */
static bigfrac_err_ty fact_term_ratio(bigfrac_ty *term, bigfrac_ty *q,
        const bigfrac_ty *r, size_t n, int start, int step) {
    bigfrac_err_ty err = frac_u64(term, 1U);
    if (start == 1 && err == BIGFRAC_OK_E) {
        err = bigfrac_copy(term, r);            /* r^1 / 1! */
    }
    /* term = r^start / start!（start ∈ {0,1}） */
    for (size_t j = 0; j < n && err == BIGFRAC_OK_E; j++) {
        bigfrac_ty denom;
        bigfrac_ty factor;
        bigfrac_init(&denom);
        bigfrac_init(&factor);
        err = frac_u64(&denom, 1U);
        for (int i = 1; i <= step && err == BIGFRAC_OK_E; i++) {
            err = frac_u64(&factor,
                           (uint64_t)(start + (int)(step * j) + i));
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_mul(&denom, &denom, &factor);
            }
        }
        bigfrac_ty rpow;
        bigfrac_init(&rpow);
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_copy(&rpow, r);
            for (int i = 1; i < step && err == BIGFRAC_OK_E; i++) {
                err = bigfrac_mul(&rpow, &rpow, r);
            }
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_div(q, &rpow, &denom);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_mul(term, term, q);   /* |t_{j+1}| = |t_j|·q_j */
        }
        bigfrac_free(&rpow);
        bigfrac_free(&factor);
        bigfrac_free(&denom);
    }
    /* 此时 term = |t_n|；仍需 q_n（比值随 j 递减，取 j=n 处） */
    if (err == BIGFRAC_OK_E) {
        bigfrac_ty denom;
        bigfrac_ty factor;
        bigfrac_init(&denom);
        bigfrac_init(&factor);
        err = frac_u64(&denom, 1U);
        for (int i = 1; i <= step && err == BIGFRAC_OK_E; i++) {
            err = frac_u64(&factor,
                           (uint64_t)(start + (int)(step * n) + i));
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_mul(&denom, &denom, &factor);
            }
        }
        bigfrac_ty rpow;
        bigfrac_init(&rpow);
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_copy(&rpow, r);
            for (int i = 1; i < step && err == BIGFRAC_OK_E; i++) {
                err = bigfrac_mul(&rpow, &rpow, r);
            }
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_div(q, &rpow, &denom);
        }
        bigfrac_free(&rpow);
        bigfrac_free(&factor);
        bigfrac_free(&denom);
    }
    return err;
}

static bigfrac_err_ty fact_order_tail(size_t *out_n, bigfrac_ty *out_tail,
        const bigfrac_ty *r, const bigfrac_ty *eps, int start, int step,
        int alt) {
    (void)alt;   /* 幅度与符号无关 */
    if (out_n == NULL || r == NULL || eps == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    bigfrac_ty zero;
    bigfrac_init(&zero);
    bigfrac_err_ty err = BIGFRAC_OK_E;
    if (bigfrac_cmp(r, &zero) < 0 || bigfrac_cmp(eps, &zero) < 0) {
        err = BIGFRAC_ERR_INVALID_E;
    }
    if (err != BIGFRAC_OK_E) {
        bigfrac_free(&zero);
        return err;
    }

    for (size_t n = 0U; n <= NEX_SERIES_ORDER_CAP; n++) {
        bigfrac_ty term;
        bigfrac_ty q;
        bigfrac_init(&term);
        bigfrac_init(&q);
        err = fact_term_ratio(&term, &q, r, n, start, step);
        if (err != BIGFRAC_OK_E) {
            bigfrac_free(&q);
            bigfrac_free(&term);
            return err;
        }
        bigfrac_ty one;
        bigfrac_ty one_minus_q;
        bigfrac_ty tail;
        bigfrac_init(&one);
        bigfrac_init(&one_minus_q);
        bigfrac_init(&tail);
        err = frac_u64(&one, 1U);
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_sub(&one_minus_q, &one, &q);
        }
        bool bounded = (err == BIGFRAC_OK_E &&
                        bigfrac_cmp(&one_minus_q, &zero) > 0);
        if (bounded) {
            err = bigfrac_div(&tail, &term, &one_minus_q);
        }
        if (err == BIGFRAC_OK_E && bounded &&
                bigfrac_cmp(&tail, eps) <= 0) {
            *out_n = n;
            if (out_tail != NULL) {
                err = bigfrac_copy(out_tail, &tail);
            }
            bigfrac_free(&tail);
            bigfrac_free(&one_minus_q);
            bigfrac_free(&one);
            bigfrac_free(&q);
            bigfrac_free(&term);
            bigfrac_free(&zero);
            return err;
        }
        bigfrac_free(&tail);
        bigfrac_free(&one_minus_q);
        bigfrac_free(&one);
        bigfrac_free(&q);
        bigfrac_free(&term);
        if (err != BIGFRAC_OK_E) {
            return err;
        }
    }
    bigfrac_free(&zero);
    return BIGFRAC_ERR_INVALID_E;   /* 超出迭代上限 */
}

bigfrac_err_ty nex_series_exp_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps) {
    return fact_order_tail(out_n, NULL, r, eps, 0, 1, 0);
}

bigfrac_err_ty nex_series_cos_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps) {
    return fact_order_tail(out_n, NULL, r, eps, 0, 2, 1);
}

bigfrac_err_ty nex_series_sin_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps) {
    return fact_order_tail(out_n, NULL, r, eps, 1, 2, 1);
}

/* 精确部分和 Σ_{j<n} (−1)^{alt·j} z^{start+step·j}/(start+step·j)! */
static bigfrac_err_ty fact_eval_frac(bigfrac_ty *out, const bigfrac_ty *z,
        size_t n, int start, int step, int alt) {
    if (n == 0U) {
        bigfrac_ty zero;
        bigfrac_init(&zero);
        bigfrac_err_ty e = bigfrac_copy(out, &zero);
        bigfrac_free(&zero);
        return e;
    }
    size_t len = (size_t)step * (n - 1U) + 1U;
    bigfrac_ty *coeffs = (bigfrac_ty *)nex_malloc(len * sizeof(bigfrac_ty));
    if (coeffs == NULL) {
        return BIGFRAC_ERR_OOM_E;
    }
    for (size_t i = 0U; i < len; i++) {
        bigfrac_init(&coeffs[i]);
    }

    bigfrac_ty fact;
    bigfrac_ty inv;
    bigfrac_init(&fact);
    bigfrac_init(&inv);
    bigfrac_err_ty err = frac_u64(&fact, 1U);   /* 0! = 1! = 1 */
    for (size_t j = 0U; j < n && err == BIGFRAC_OK_E; j++) {
        size_t idx = (size_t)step * j;
        err = bigfrac_copy(&inv, &fact);
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_inv(&inv);            /* 1/(start+step·j)! */
        }
        if (err == BIGFRAC_OK_E && alt != 0 && (j & 1U) != 0U) {
            err = bigfrac_neg(&inv);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_copy(&coeffs[idx], &inv);
        }
        /* fact *= Π (start+step·j+i)，到 (start+step·(j+1))! */
        for (int i = 1; i <= step && err == BIGFRAC_OK_E; i++) {
            bigfrac_ty factor;
            bigfrac_init(&factor);
            err = frac_u64(&factor,
                           (uint64_t)(start + (int)(step * j) + i));
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_mul(&fact, &fact, &factor);
            }
            bigfrac_free(&factor);
        }
    }

    bigfrac_series_ty s;
    bigfrac_series_init(&s);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_from_array(&s, (int64_t)start, coeffs, len);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_eval(out, &s, z);
    }

    bigfrac_series_free(&s);
    bigfrac_free(&inv);
    bigfrac_free(&fact);
    for (size_t i = 0U; i < len; i++) {
        bigfrac_free(&coeffs[i]);
    }
    free(coeffs);
    return err;
}

/* ------------------------------------------------------------------ */
/* 错误映射与数值求值                                                  */
/* ------------------------------------------------------------------ */

static bigfloat_err_ty map_frac_err(bigfrac_err_ty err) {
    return (err == BIGFRAC_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                      : BIGFLOAT_ERR_INVALID_E;
}

static bigdecimal_err_ty map_frac_err_dec(bigfrac_err_ty err) {
    return (err == BIGFRAC_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                      : BIGDECIMAL_ERR_INVALID_E;
}

/* 公共：把截断尾上界 tail（bigfrac，≥0）输出到 err_out（可空） */
static nex_convert_err_ty emit_bound_float(bigfloat_ty *err_out,
        const bigfrac_ty *tail, const bigfloat_ctx_ty *ctx) {
    if (err_out == NULL) {
        return NEX_CONVERT_OK_E;
    }
    return nex_convert_frac_to_float(err_out, tail, ctx);
}

static nex_convert_err_ty emit_bound_decimal(bigdecimal_ty *err_out,
        const bigfrac_ty *tail, const bigdecimal_ctx_ty *ctx) {
    if (err_out == NULL) {
        return NEX_CONVERT_OK_E;
    }
    return nex_convert_frac_to_decimal(err_out, tail, ctx);
}

bigfloat_err_ty nex_series_binom_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfrac_ty *alpha, const bigfloat_ty *z,
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

    bigfrac_ty A;
    bigfrac_ty B;
    bigfrac_ty tail;
    bigfrac_init(&A);
    bigfrac_init(&B);
    bigfrac_init(&tail);
    size_t n = 0U;
    if (err == BIGFRAC_OK_E) {
        err = nex_series_binom_model(&A, &B, alpha);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_order_geometric(&n, &A, &B, &r, eps);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_tail_geometric(&tail, &A, &B, &r, n);
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
        if (conv == NEX_CONVERT_OK_E) {
            conv = emit_bound_float(err_out, &tail, ctx);
        }
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                                     : BIGFLOAT_ERR_INVALID_E;
        }
    }

    bigfrac_free(&partial);
    bigfrac_series_free(&s);
    bigfrac_free(&tail);
    bigfrac_free(&B);
    bigfrac_free(&A);
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}

bigdecimal_err_ty nex_series_binom_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigfrac_ty *alpha,
        const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx) {
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

    bigfrac_ty A;
    bigfrac_ty B;
    bigfrac_ty tail;
    bigfrac_init(&A);
    bigfrac_init(&B);
    bigfrac_init(&tail);
    size_t n = 0U;
    if (err == BIGFRAC_OK_E) {
        err = nex_series_binom_model(&A, &B, alpha);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_order_geometric(&n, &A, &B, &r, eps);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_tail_geometric(&tail, &A, &B, &r, n);
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
        if (conv == NEX_CONVERT_OK_E) {
            conv = emit_bound_decimal(err_out, &tail, ctx);
        }
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                                     : BIGDECIMAL_ERR_INVALID_E;
        }
    }

    bigfrac_free(&partial);
    bigfrac_series_free(&s);
    bigfrac_free(&tail);
    bigfrac_free(&B);
    bigfrac_free(&A);
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}

/* 通用阶乘型数值求值（bigfloat） */
static bigfloat_err_ty fact_eval_float(bigfloat_ty *out, bigfloat_ty *err_out,
        const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx, int start, int step, int alt) {
    if (out == NULL || z == NULL || eps == NULL || ctx == NULL) {
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
    bigfrac_ty tail;
    bigfrac_init(&r);
    bigfrac_init(&tail);
    bigfrac_err_ty err = frac_abs(&r, &zf);
    size_t n = 0U;
    if (err == BIGFRAC_OK_E) {
        err = fact_order_tail(&n, &tail, &r, eps, start, step, alt);
    }
    bigfrac_ty partial;
    bigfrac_init(&partial);
    if (err == BIGFRAC_OK_E) {
        err = fact_eval_frac(&partial, &zf, n, start, step, alt);
    }
    bigfloat_err_ty result = BIGFLOAT_OK_E;
    if (err != BIGFRAC_OK_E) {
        result = map_frac_err(err);
    } else {
        conv = nex_convert_frac_to_float(out, &partial, ctx);
        if (conv == NEX_CONVERT_OK_E) {
            conv = emit_bound_float(err_out, &tail, ctx);
        }
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                                     : BIGFLOAT_ERR_INVALID_E;
        }
    }
    bigfrac_free(&partial);
    bigfrac_free(&tail);
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}

/* 通用阶乘型数值求值（bigdecimal） */
static bigdecimal_err_ty fact_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx, int start,
        int step, int alt) {
    if (out == NULL || z == NULL || eps == NULL || ctx == NULL) {
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
    bigfrac_ty tail;
    bigfrac_init(&r);
    bigfrac_init(&tail);
    bigfrac_err_ty err = frac_abs(&r, &zf);
    size_t n = 0U;
    if (err == BIGFRAC_OK_E) {
        err = fact_order_tail(&n, &tail, &r, eps, start, step, alt);
    }
    bigfrac_ty partial;
    bigfrac_init(&partial);
    if (err == BIGFRAC_OK_E) {
        err = fact_eval_frac(&partial, &zf, n, start, step, alt);
    }
    bigdecimal_err_ty result = BIGDECIMAL_OK_E;
    if (err != BIGFRAC_OK_E) {
        result = map_frac_err_dec(err);
    } else {
        conv = nex_convert_frac_to_decimal(out, &partial, ctx);
        if (conv == NEX_CONVERT_OK_E) {
            conv = emit_bound_decimal(err_out, &tail, ctx);
        }
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                                     : BIGDECIMAL_ERR_INVALID_E;
        }
    }
    bigfrac_free(&partial);
    bigfrac_free(&tail);
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}

bigfloat_err_ty nex_series_exp_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx) {
    return fact_eval_float(out, err_out, z, eps, ctx, 0, 1, 0);
}

bigfloat_err_ty nex_series_cos_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx) {
    return fact_eval_float(out, err_out, z, eps, ctx, 0, 2, 1);
}

bigfloat_err_ty nex_series_sin_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx) {
    return fact_eval_float(out, err_out, z, eps, ctx, 1, 2, 1);
}

bigdecimal_err_ty nex_series_exp_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx) {
    return fact_eval_decimal(out, err_out, z, eps, ctx, 0, 1, 0);
}

bigdecimal_err_ty nex_series_cos_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx) {
    return fact_eval_decimal(out, err_out, z, eps, ctx, 0, 2, 1);
}

bigdecimal_err_ty nex_series_sin_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx) {
    return fact_eval_decimal(out, err_out, z, eps, ctx, 1, 2, 1);
}
