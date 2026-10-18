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

/* 工作精度：p + 48 + ceil(log2 n)（覆盖 ~3n 次递推舍入，余量 > 40 bit） */
static size_t series_guard_bits(size_t n) {
    size_t g = 48U;
    size_t m = n;
    while (m > 1U) {
        g += 1U;
        m >>= 1U;
    }
    return g;
}

static bigfloat_err_ty series_bf_u64(bigfloat_ty *out, uint64_t v,
        const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty bi;
    bigint_bin_init(&bi);
    bigint_err_ty ierr = bigint_bin_from_u64(&bi, v);
    bigfloat_err_ty err;
    if (ierr == BIGINT_OK_E) {
        err = bigfloat_from_bigint(out, &bi, ctx);
    } else {
        err = (ierr == BIGINT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                         : BIGFLOAT_ERR_INVALID_E;
    }
    bigint_bin_free(&bi);
    return err;
}

static bigdecimal_err_ty series_bd_u64(bigdecimal_ty *out, uint64_t v,
        const bigdecimal_ctx_ty *ctx) {
    bigint_dec_ty bi;
    bigint_dec_init(&bi);
    bigint_err_ty ierr = bigint_dec_from_u64(&bi, v);
    bigdecimal_err_ty err;
    if (ierr == BIGINT_OK_E) {
        err = bigdecimal_from_bigint(out, &bi, ctx);
    } else {
        err = (ierr == BIGINT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                         : BIGDECIMAL_ERR_INVALID_E;
    }
    bigint_dec_free(&bi);
    return err;
}

/* 工作精度结果一次性舍入到 ctx（误差远低于 ctx 的 1 ulp） */
static bigfloat_err_ty series_bf_round(bigfloat_ty *out,
        const bigfloat_ty *val, const bigfloat_ctx_ty *ctx) {
    bigint_bin_ty mant;
    bigint_bin_init(&mant);
    int64_t e = 0;
    bigfloat_flag_ty fl = BIGFLOAT_POS_ZERO_E;
    bigfloat_err_ty err = bigfloat_decompose(val, &mant, &e, &fl);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_compose(out, &mant, e, fl, ctx);
    }
    bigint_bin_free(&mant);
    return err;
}

static bigdecimal_err_ty series_bd_round(bigdecimal_ty *out,
        const bigdecimal_ty *val, const bigdecimal_ctx_ty *ctx) {
    bigint_dec_ty mant;
    bigint_dec_init(&mant);
    int64_t e = 0;
    bigdecimal_flag_ty fl = BIGDECIMAL_POS_ZERO_E;
    bigdecimal_err_ty err = bigdecimal_decompose(val, &mant, &e, &fl);
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_compose(out, &mant, e, fl, ctx);
    }
    bigint_dec_free(&mant);
    return err;
}

/* 扩展精度阶估计：用定向舍入的 bigfloat 递推尾界
   （尾界只用于选阶与 err_out；比精确 bigfrac 快一个量级以上）。 */
static bigfloat_err_ty series_bf_abs(bigfloat_ty *out,
        const bigfloat_ty *x) {
    bigfloat_err_ty err = bigfloat_copy(out, x);
    if (err == BIGFLOAT_OK_E && x->flag == BIGFLOAT_NEG_E) {
        err = bigfloat_neg(out);
    }
    return err;
}

static bigdecimal_err_ty series_bd_abs(bigdecimal_ty *out,
        const bigdecimal_ty *x) {
    bigdecimal_err_ty err = bigdecimal_copy(out, x);
    if (err == BIGDECIMAL_OK_E && x->flag == BIGDECIMAL_NEG_E) {
        err = bigdecimal_neg(out);
    }
    return err;
}

/* tail（扩展精度、向上舍入）→ err_out（ctx 精度、向上舍入） */
static bigfloat_err_ty series_bf_emit_upper(bigfloat_ty *err_out,
        const bigfloat_ty *tail, const bigfloat_ctx_ty *ctx) {
    if (err_out == NULL) {
        return BIGFLOAT_OK_E;
    }
    bigfloat_ctx_ty up;
    bigfloat_err_ty err = bigfloat_ctx_make(&up, ctx->mant_bits,
            ctx->exp_bits, BIGFLOAT_ROUND_TOWARD_POS_E);
    bigint_bin_ty mant;
    bigint_bin_init(&mant);
    int64_t e = 0;
    bigfloat_flag_ty fl = BIGFLOAT_POS_ZERO_E;
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_decompose(tail, &mant, &e, &fl);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_compose(err_out, &mant, e, fl, &up);
    }
    bigint_bin_free(&mant);
    return err;
}

static bigdecimal_err_ty series_bd_emit_upper(bigdecimal_ty *err_out,
        const bigdecimal_ty *tail, const bigdecimal_ctx_ty *ctx) {
    if (err_out == NULL) {
        return BIGDECIMAL_OK_E;
    }
    bigdecimal_ctx_ty up;
    bigdecimal_err_ty err = bigdecimal_ctx_make(&up, ctx->mant_digits,
            ctx->exp_digits, BIGDECIMAL_ROUND_TOWARD_POS_E);
    bigint_dec_ty mant;
    bigint_dec_init(&mant);
    int64_t e = 0;
    bigdecimal_flag_ty fl = BIGDECIMAL_POS_ZERO_E;
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_decompose(tail, &mant, &e, &fl);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_compose(err_out, &mant, e, fl, &up);
    }
    bigint_dec_free(&mant);
    return err;
}

/* 阶乘型：返回最小 n 与尾界（bigfloat 上界） */
static bigfloat_err_ty fact_order_fast(size_t *out_n, bigfloat_ty *out_tail,
        const bigfloat_ty *z, const bigfrac_ty *eps, int start, int step,
        int alt, const bigfloat_ctx_ty *ctx) {
    (void)alt;   /* 幅度与符号无关 */
    if (out_n == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (ctx->mant_bits > SIZE_MAX - 32U) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    const size_t wp = ctx->mant_bits + 32U;
    bigfloat_ctx_ty wup;
    bigfloat_ctx_ty wdn;
    bigfloat_err_ty err = bigfloat_ctx_make(&wup, wp, ctx->exp_bits,
            BIGFLOAT_ROUND_TOWARD_POS_E);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ctx_make(&wdn, wp, ctx->exp_bits,
                BIGFLOAT_ROUND_TOWARD_NEG_E);
    }
    bigfloat_ty eps_bf;
    bigfloat_ty az;
    bigfloat_ty u;
    bigfloat_ty term;
    bigfloat_ty q;
    bigfloat_ty one;
    bigfloat_ty omq;
    bigfloat_ty tail;
    bigfloat_ty f;
    bigfloat_init(&eps_bf);
    bigfloat_init(&az);
    bigfloat_init(&u);
    bigfloat_init(&term);
    bigfloat_init(&q);
    bigfloat_init(&one);
    bigfloat_init(&omq);
    bigfloat_init(&tail);
    bigfloat_init(&f);
    if (err == BIGFLOAT_OK_E) {
        nex_convert_err_ty conv = nex_convert_frac_to_float(&eps_bf, eps,
                &wdn);
        if (conv != NEX_CONVERT_OK_E) {
            err = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                                  : BIGFLOAT_ERR_INVALID_E;
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = series_bf_abs(&az, z);
    }
    if (err == BIGFLOAT_OK_E) {
        err = (step == 1) ? bigfloat_copy(&u, &az)
                          : bigfloat_mul(&u, &az, &az, &wup);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&one, 1.0);
    }
    if (err == BIGFLOAT_OK_E) {
        if (start == 0) {
            err = bigfloat_from_f64(&term, 1.0);
        } else {
            err = bigfloat_copy(&term, &az);
        }
    }
    for (size_t n = 0U; n <= NEX_SERIES_ORDER_CAP && err == BIGFLOAT_OK_E;
            n++) {
        /* q_n = |z|^step / Π_{i=1..step}(start+step·n+i) */
        err = bigfloat_from_f64(&q, 1.0);
        for (int i = 1; i <= step && err == BIGFLOAT_OK_E; i++) {
            const uint64_t d = (uint64_t)(start + (int)(step * n) + i);
            err = series_bf_u64(&f, d, &wup);
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_div(&q, &q, &f, &wup);
            }
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_mul(&q, &q, &u, &wup);
        }
        bool bounded = false;
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_sub(&omq, &one, &q, &wdn);
            bounded = !bigfloat_is_zero(&omq)
                    && omq.flag != BIGFLOAT_NEG_E;
        }
        if (err == BIGFLOAT_OK_E && bounded) {
            err = bigfloat_div(&tail, &term, &omq, &wup);
            if (err == BIGFLOAT_OK_E
                    && bigfloat_cmp(&tail, &eps_bf) <= 0) {
                *out_n = n;
                if (out_tail != NULL) {
                    err = bigfloat_copy(out_tail, &tail);
                }
                break;
            }
        }
        if (n == NEX_SERIES_ORDER_CAP) {
            err = BIGFLOAT_ERR_INVALID_E;
        }
        if (err == BIGFLOAT_OK_E) {
            /* term_{n+1} = term_n·u / Π_{i=1..step}(k+i) */
            err = bigfloat_mul(&term, &term, &u, &wup);
            for (int i = 1; i <= step && err == BIGFLOAT_OK_E; i++) {
                const uint64_t d = (uint64_t)(start + (int)(step * n) + i);
                err = series_bf_u64(&f, d, &wup);
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_div(&term, &term, &f, &wup);
                }
            }
        }
    }
    bigfloat_free(&f);
    bigfloat_free(&tail);
    bigfloat_free(&omq);
    bigfloat_free(&one);
    bigfloat_free(&q);
    bigfloat_free(&term);
    bigfloat_free(&u);
    bigfloat_free(&az);
    bigfloat_free(&eps_bf);
    return err;
}

/* 逆幂型：返回最小 n 与尾界（几何尾界 term/(1−q)） */
static bigfloat_err_ty invpow_order_fast(size_t *out_n,
        bigfloat_ty *out_tail, const bigfloat_ty *z, const bigfrac_ty *eps,
        int start, int step, int den0, const bigfloat_ctx_ty *ctx) {
    if (out_n == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    if (ctx->mant_bits > SIZE_MAX - 32U) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    const size_t wp = ctx->mant_bits + 32U;
    bigfloat_ctx_ty wup;
    bigfloat_ctx_ty wdn;
    bigfloat_err_ty err = bigfloat_ctx_make(&wup, wp, ctx->exp_bits,
            BIGFLOAT_ROUND_TOWARD_POS_E);
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_ctx_make(&wdn, wp, ctx->exp_bits,
                BIGFLOAT_ROUND_TOWARD_NEG_E);
    }
    bigfloat_ty eps_bf;
    bigfloat_ty az;
    bigfloat_ty u;
    bigfloat_ty term;
    bigfloat_ty one;
    bigfloat_ty omq;
    bigfloat_ty tail;
    bigfloat_ty f;
    bigfloat_init(&eps_bf);
    bigfloat_init(&az);
    bigfloat_init(&u);
    bigfloat_init(&term);
    bigfloat_init(&one);
    bigfloat_init(&omq);
    bigfloat_init(&tail);
    bigfloat_init(&f);
    if (err == BIGFLOAT_OK_E) {
        nex_convert_err_ty conv = nex_convert_frac_to_float(&eps_bf, eps,
                &wdn);
        if (conv != NEX_CONVERT_OK_E) {
            err = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                                  : BIGFLOAT_ERR_INVALID_E;
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = series_bf_abs(&az, z);
    }
    if (err == BIGFLOAT_OK_E) {
        err = (step == 1) ? bigfloat_copy(&u, &az)
                          : bigfloat_mul(&u, &az, &az, &wup);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_from_f64(&one, 1.0);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_sub(&omq, &one, &u, &wdn);
        if (bigfloat_is_zero(&omq) || omq.flag == BIGFLOAT_NEG_E) {
            err = BIGFLOAT_ERR_INVALID_E;   /* 需 |z|^step < 1 */
        }
    }
    if (err == BIGFLOAT_OK_E) {
        if (start == 0) {
            err = bigfloat_from_f64(&term, 1.0);
        } else {
            err = bigfloat_copy(&term, &az);
        }
        if (err == BIGFLOAT_OK_E) {
            err = series_bf_u64(&f, (uint64_t)den0, &wup);
        }
        if (err == BIGFLOAT_OK_E) {
            err = bigfloat_div(&term, &term, &f, &wup);
        }
    }
    for (size_t n = 0U; n <= NEX_SERIES_ORDER_CAP && err == BIGFLOAT_OK_E;
            n++) {
        err = bigfloat_div(&tail, &term, &omq, &wup);
        if (err == BIGFLOAT_OK_E && bigfloat_cmp(&tail, &eps_bf) <= 0) {
            *out_n = n;
            if (out_tail != NULL) {
                err = bigfloat_copy(out_tail, &tail);
            }
            break;
        }
        if (n == NEX_SERIES_ORDER_CAP) {
            err = BIGFLOAT_ERR_INVALID_E;
        }
        if (err == BIGFLOAT_OK_E) {
            /* term *= u·(den0+step·n)/(den0+step·(n+1)) */
            err = bigfloat_mul(&term, &term, &u, &wup);
            if (err == BIGFLOAT_OK_E) {
                err = series_bf_u64(&f,
                        (uint64_t)(den0 + step * (int)n), &wup);
            }
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_mul(&term, &term, &f, &wup);
            }
            if (err == BIGFLOAT_OK_E) {
                err = series_bf_u64(&f,
                        (uint64_t)(den0 + step * (int)(n + 1U)), &wup);
            }
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_div(&term, &term, &f, &wup);
            }
        }
    }
    bigfloat_free(&f);
    bigfloat_free(&tail);
    bigfloat_free(&omq);
    bigfloat_free(&one);
    bigfloat_free(&term);
    bigfloat_free(&u);
    bigfloat_free(&az);
    bigfloat_free(&eps_bf);
    return err;
}

/* decimal 阶估计：|z| 向上转 bigfloat 复用 float 快速阶估计，尾界转回 decimal */
static bigdecimal_err_ty fact_order_fast_dec(size_t *out_n,
        bigdecimal_ty *out_tail, const bigdecimal_ty *z,
        const bigfrac_ty *eps, int start, int step, int alt,
        const bigdecimal_ctx_ty *ctx) {
    if (out_n == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    const size_t pb = (ctx->mant_digits * 3322U) / 1000U + 32U;
    if (pb > SIZE_MAX - 32U) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigfloat_ctx_ty octx;
    bigfloat_ctx_ty oup;
    bigfloat_err_ty ferr = bigfloat_ctx_make(&octx, pb, 61U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E);
    if (ferr == BIGFLOAT_OK_E) {
        ferr = bigfloat_ctx_make(&oup, pb, 61U,
                BIGFLOAT_ROUND_TOWARD_POS_E);
    }
    bigdecimal_ty az;
    bigfloat_ty zbf;
    bigfloat_ty tailbf;
    bigdecimal_init(&az);
    bigfloat_init(&zbf);
    bigfloat_init(&tailbf);
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    if (ferr == BIGFLOAT_OK_E) {
        err = series_bd_abs(&az, z);
    }
    if (err == BIGDECIMAL_OK_E) {
        nex_convert_err_ty conv = nex_convert_decimal_to_float(&zbf, &az,
                &oup);
        if (conv != NEX_CONVERT_OK_E) {
            err = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                                  : BIGDECIMAL_ERR_INVALID_E;
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        ferr = fact_order_fast(out_n,
                (out_tail != NULL) ? &tailbf : NULL, &zbf, eps, start, step,
                alt, &octx);
        if (ferr == BIGFLOAT_ERR_OOM_E) {
            err = BIGDECIMAL_ERR_OOM_E;
        } else if (ferr != BIGFLOAT_OK_E) {
            err = BIGDECIMAL_ERR_INVALID_E;
        }
    }
    if (err == BIGDECIMAL_OK_E && out_tail != NULL) {
        bigdecimal_ctx_ty dup;
        err = bigdecimal_ctx_make(&dup, ctx->mant_digits, ctx->exp_digits,
                BIGDECIMAL_ROUND_TOWARD_POS_E);
        if (err == BIGDECIMAL_OK_E) {
            nex_convert_err_ty conv = nex_convert_float_to_decimal(out_tail,
                    &tailbf, &dup);
            if (conv != NEX_CONVERT_OK_E) {
                err = (conv == NEX_CONVERT_ERR_OOM_E)
                        ? BIGDECIMAL_ERR_OOM_E
                        : BIGDECIMAL_ERR_INVALID_E;
            }
        }
    }
    bigfloat_free(&tailbf);
    bigfloat_free(&zbf);
    bigdecimal_free(&az);
    return err;
}

static bigdecimal_err_ty invpow_order_fast_dec(size_t *out_n,
        bigdecimal_ty *out_tail, const bigdecimal_ty *z,
        const bigfrac_ty *eps, int start, int step, int den0,
        const bigdecimal_ctx_ty *ctx) {
    if (out_n == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    const size_t pb = (ctx->mant_digits * 3322U) / 1000U + 32U;
    if (pb > SIZE_MAX - 32U) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigfloat_ctx_ty octx;
    bigfloat_ctx_ty oup;
    bigfloat_err_ty ferr = bigfloat_ctx_make(&octx, pb, 61U,
            BIGFLOAT_ROUND_NEAREST_EVEN_E);
    if (ferr == BIGFLOAT_OK_E) {
        ferr = bigfloat_ctx_make(&oup, pb, 61U,
                BIGFLOAT_ROUND_TOWARD_POS_E);
    }
    bigdecimal_ty az;
    bigfloat_ty zbf;
    bigfloat_ty tailbf;
    bigdecimal_init(&az);
    bigfloat_init(&zbf);
    bigfloat_init(&tailbf);
    bigdecimal_err_ty err = BIGDECIMAL_OK_E;
    if (ferr == BIGFLOAT_OK_E) {
        err = series_bd_abs(&az, z);
    }
    if (err == BIGDECIMAL_OK_E) {
        nex_convert_err_ty conv = nex_convert_decimal_to_float(&zbf, &az,
                &oup);
        if (conv != NEX_CONVERT_OK_E) {
            err = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                                  : BIGDECIMAL_ERR_INVALID_E;
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        ferr = invpow_order_fast(out_n,
                (out_tail != NULL) ? &tailbf : NULL, &zbf, eps, start, step,
                den0, &octx);
        if (ferr == BIGFLOAT_ERR_OOM_E) {
            err = BIGDECIMAL_ERR_OOM_E;
        } else if (ferr != BIGFLOAT_OK_E) {
            err = BIGDECIMAL_ERR_INVALID_E;
        }
    }
    if (err == BIGDECIMAL_OK_E && out_tail != NULL) {
        bigdecimal_ctx_ty dup;
        err = bigdecimal_ctx_make(&dup, ctx->mant_digits, ctx->exp_digits,
                BIGDECIMAL_ROUND_TOWARD_POS_E);
        if (err == BIGDECIMAL_OK_E) {
            nex_convert_err_ty conv = nex_convert_float_to_decimal(out_tail,
                    &tailbf, &dup);
            if (conv != NEX_CONVERT_OK_E) {
                err = (conv == NEX_CONVERT_ERR_OOM_E)
                        ? BIGDECIMAL_ERR_OOM_E
                        : BIGDECIMAL_ERR_INVALID_E;
            }
        }
    }
    bigfloat_free(&tailbf);
    bigfloat_free(&zbf);
    bigdecimal_free(&az);
    return err;
}

/* 阶乘型部分和：Σ_{j<n} (−1)^{alt·j} z^{start+step·j}/(start+step·j)!
   （extended precision 递推；err_out 仍只输出截断尾界） */
static bigfloat_err_ty fact_sum_float(bigfloat_ty *out, bigfloat_ty *err_out,
        const bigfloat_ty *z, const bigfloat_ty *tail, size_t n,
        int start, int step, int alt, const bigfloat_ctx_ty *ctx) {
    const size_t guard = series_guard_bits(n);
    if (ctx->mant_bits > SIZE_MAX - guard) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ctx_ty wctx;
    bigfloat_err_ty err = bigfloat_ctx_make(&wctx,
            ctx->mant_bits + guard, ctx->exp_bits,
            BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_ty u;
    bigfloat_ty term;
    bigfloat_ty sum;
    bigfloat_ty d;
    bigfloat_init(&u);
    bigfloat_init(&term);
    bigfloat_init(&sum);
    bigfloat_init(&d);
    if (err == BIGFLOAT_OK_E) {
        err = (step == 1) ? bigfloat_copy(&u, z)
                          : bigfloat_mul(&u, z, z, &wctx);
    }
    if (err == BIGFLOAT_OK_E) {
        if (n == 0U) {
            err = bigfloat_from_f64(&sum, 0.0);
        } else {
            err = (start == 0) ? bigfloat_from_f64(&term, 1.0)
                               : bigfloat_copy(&term, z);
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_copy(&sum, &term);
            }
            for (size_t j = 1U; j < n && err == BIGFLOAT_OK_E; j++) {
                err = bigfloat_mul(&term, &term, &u, &wctx);
                if (err == BIGFLOAT_OK_E && alt != 0) {
                    err = bigfloat_neg(&term);
                }
                uint64_t denom;
                if (step == 1) {
                    denom = (uint64_t)j;
                } else if (start == 0) {
                    const uint64_t k = 2U * (uint64_t)j;
                    denom = (k - 1U) * k;
                } else {
                    const uint64_t k = 2U * (uint64_t)j;
                    denom = k * (k + 1U);
                }
                if (err == BIGFLOAT_OK_E) {
                    err = series_bf_u64(&d, denom, &wctx);
                }
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_div(&term, &term, &d, &wctx);
                }
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_add(&sum, &sum, &term, &wctx);
                }
            }
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = series_bf_emit_upper(err_out, tail, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = series_bf_round(out, &sum, ctx);
    }
    bigfloat_free(&d);
    bigfloat_free(&sum);
    bigfloat_free(&term);
    bigfloat_free(&u);
    return err;
}

/* 阶乘型部分和（bigdecimal 版） */
static bigdecimal_err_ty fact_sum_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigdecimal_ty *tail, size_t n, int start, int step, int alt,
        const bigdecimal_ctx_ty *ctx) {
    const size_t guard = series_guard_bits(n);
    const size_t extra = guard / 3U + 2U;
    if (ctx->mant_digits > SIZE_MAX - extra) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ctx_ty wctx;
    bigdecimal_err_ty err = bigdecimal_ctx_make(&wctx,
            ctx->mant_digits + extra, ctx->exp_digits,
            BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_ty u;
    bigdecimal_ty term;
    bigdecimal_ty sum;
    bigdecimal_ty d;
    bigdecimal_init(&u);
    bigdecimal_init(&term);
    bigdecimal_init(&sum);
    bigdecimal_init(&d);
    if (err == BIGDECIMAL_OK_E) {
        err = (step == 1) ? bigdecimal_copy(&u, z)
                          : bigdecimal_mul(&u, z, z, &wctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        if (n == 0U) {
            err = bigdecimal_from_str(&sum, "0", &wctx, NULL);
        } else {
            err = (start == 0) ? bigdecimal_from_str(&term, "1", &wctx, NULL)
                               : bigdecimal_copy(&term, z);
            if (err == BIGDECIMAL_OK_E) {
                err = bigdecimal_copy(&sum, &term);
            }
            for (size_t j = 1U; j < n && err == BIGDECIMAL_OK_E; j++) {
                err = bigdecimal_mul(&term, &term, &u, &wctx);
                if (err == BIGDECIMAL_OK_E && alt != 0) {
                    err = bigdecimal_neg(&term);
                }
                uint64_t denom;
                if (step == 1) {
                    denom = (uint64_t)j;
                } else if (start == 0) {
                    const uint64_t k = 2U * (uint64_t)j;
                    denom = (k - 1U) * k;
                } else {
                    const uint64_t k = 2U * (uint64_t)j;
                    denom = k * (k + 1U);
                }
                if (err == BIGDECIMAL_OK_E) {
                    err = series_bd_u64(&d, denom, &wctx);
                }
                if (err == BIGDECIMAL_OK_E) {
                    err = bigdecimal_div(&term, &term, &d, &wctx);
                }
                if (err == BIGDECIMAL_OK_E) {
                    err = bigdecimal_add(&sum, &sum, &term, &wctx);
                }
            }
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        err = series_bd_emit_upper(err_out, tail, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = series_bd_round(out, &sum, ctx);
    }
    bigdecimal_free(&d);
    bigdecimal_free(&sum);
    bigdecimal_free(&term);
    bigdecimal_free(&u);
    return err;
}

/* 逆幂型部分和：Σ_{j<n} (−1)^{alt·j} z^{start+step·j}/(den0+step·j) */
static bigfloat_err_ty invpow_sum_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfloat_ty *tail,
        size_t n, int start, int step, int den0, int alt,
        const bigfloat_ctx_ty *ctx) {
    const size_t guard = series_guard_bits(n);
    if (ctx->mant_bits > SIZE_MAX - guard) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    bigfloat_ctx_ty wctx;
    bigfloat_err_ty err = bigfloat_ctx_make(&wctx,
            ctx->mant_bits + guard, ctx->exp_bits,
            BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_ty u;
    bigfloat_ty p;
    bigfloat_ty term;
    bigfloat_ty sum;
    bigfloat_ty d;
    bigfloat_init(&u);
    bigfloat_init(&p);
    bigfloat_init(&term);
    bigfloat_init(&sum);
    bigfloat_init(&d);
    if (err == BIGFLOAT_OK_E) {
        err = (step == 1) ? bigfloat_copy(&u, z)
                          : bigfloat_mul(&u, z, z, &wctx);
    }
    if (err == BIGFLOAT_OK_E) {
        if (n == 0U) {
            err = bigfloat_from_f64(&sum, 0.0);
        } else {
            err = (start == 0) ? bigfloat_from_f64(&p, 1.0)
                               : bigfloat_copy(&p, z);
            if (err == BIGFLOAT_OK_E) {
                err = series_bf_u64(&d, (uint64_t)den0, &wctx);
            }
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_div(&term, &p, &d, &wctx);
            }
            if (err == BIGFLOAT_OK_E) {
                err = bigfloat_copy(&sum, &term);
            }
            for (size_t j = 1U; j < n && err == BIGFLOAT_OK_E; j++) {
                err = bigfloat_mul(&p, &p, &u, &wctx);
                if (err == BIGFLOAT_OK_E) {
                    err = series_bf_u64(&d,
                            (uint64_t)(den0 + step * (int)j), &wctx);
                }
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_div(&term, &p, &d, &wctx);
                }
                if (err == BIGFLOAT_OK_E && alt != 0 && (j & 1U) != 0U) {
                    err = bigfloat_neg(&term);
                }
                if (err == BIGFLOAT_OK_E) {
                    err = bigfloat_add(&sum, &sum, &term, &wctx);
                }
            }
        }
    }
    if (err == BIGFLOAT_OK_E) {
        err = series_bf_emit_upper(err_out, tail, ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = series_bf_round(out, &sum, ctx);
    }
    bigfloat_free(&d);
    bigfloat_free(&sum);
    bigfloat_free(&term);
    bigfloat_free(&p);
    bigfloat_free(&u);
    return err;
}

/* 逆幂型部分和（bigdecimal 版） */
static bigdecimal_err_ty invpow_sum_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigdecimal_ty *tail, size_t n, int start, int step, int den0,
        int alt, const bigdecimal_ctx_ty *ctx) {
    const size_t guard = series_guard_bits(n);
    const size_t extra = guard / 3U + 2U;
    if (ctx->mant_digits > SIZE_MAX - extra) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    bigdecimal_ctx_ty wctx;
    bigdecimal_err_ty err = bigdecimal_ctx_make(&wctx,
            ctx->mant_digits + extra, ctx->exp_digits,
            BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_ty u;
    bigdecimal_ty p;
    bigdecimal_ty term;
    bigdecimal_ty sum;
    bigdecimal_ty d;
    bigdecimal_init(&u);
    bigdecimal_init(&p);
    bigdecimal_init(&term);
    bigdecimal_init(&sum);
    bigdecimal_init(&d);
    if (err == BIGDECIMAL_OK_E) {
        err = (step == 1) ? bigdecimal_copy(&u, z)
                          : bigdecimal_mul(&u, z, z, &wctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        if (n == 0U) {
            err = bigdecimal_from_str(&sum, "0", &wctx, NULL);
        } else {
            err = (start == 0) ? bigdecimal_from_str(&p, "1", &wctx, NULL)
                               : bigdecimal_copy(&p, z);
            if (err == BIGDECIMAL_OK_E) {
                err = series_bd_u64(&d, (uint64_t)den0, &wctx);
            }
            if (err == BIGDECIMAL_OK_E) {
                err = bigdecimal_div(&term, &p, &d, &wctx);
            }
            if (err == BIGDECIMAL_OK_E) {
                err = bigdecimal_copy(&sum, &term);
            }
            for (size_t j = 1U; j < n && err == BIGDECIMAL_OK_E; j++) {
                err = bigdecimal_mul(&p, &p, &u, &wctx);
                if (err == BIGDECIMAL_OK_E) {
                    err = series_bd_u64(&d,
                            (uint64_t)(den0 + step * (int)j), &wctx);
                }
                if (err == BIGDECIMAL_OK_E) {
                    err = bigdecimal_div(&term, &p, &d, &wctx);
                }
                if (err == BIGDECIMAL_OK_E && alt != 0 && (j & 1U) != 0U) {
                    err = bigdecimal_neg(&term);
                }
                if (err == BIGDECIMAL_OK_E) {
                    err = bigdecimal_add(&sum, &sum, &term, &wctx);
                }
            }
        }
    }
    if (err == BIGDECIMAL_OK_E) {
        err = series_bd_emit_upper(err_out, tail, ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = series_bd_round(out, &sum, ctx);
    }
    bigdecimal_free(&d);
    bigdecimal_free(&sum);
    bigdecimal_free(&term);
    bigdecimal_free(&p);
    bigdecimal_free(&u);
    return err;
}

/* 通用阶乘型数值求值（bigfloat） */
static bigfloat_err_ty fact_eval_float(bigfloat_ty *out, bigfloat_ty *err_out,
        const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx, int start, int step, int alt) {
    if (out == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    size_t n = 0U;
    bigfloat_ty tail;
    bigfloat_init(&tail);
    bigfloat_err_ty err = fact_order_fast(&n, &tail, z, eps, start, step,
            alt, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = fact_sum_float(out, err_out, z, &tail, n, start, step, alt,
                ctx);
    }
    bigfloat_free(&tail);
    return err;
}



/* 通用阶乘型数值求值（bigdecimal） */
static bigdecimal_err_ty fact_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx, int start,
        int step, int alt) {
    if (out == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    size_t n = 0U;
    bigdecimal_ty tail;
    bigdecimal_init(&tail);
    bigdecimal_err_ty err = fact_order_fast_dec(&n, &tail, z, eps, start,
            step, alt, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = fact_sum_decimal(out, err_out, z, &tail, n, start, step, alt,
                ctx);
    }
    bigdecimal_free(&tail);
    return err;
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

/* ------------------------------------------------------------------ */
/* 逆幂型级数 ln(1+z) / atan(z)（常数比值尾界）                        */
/*   t_j = z^{start+step·j}/(den0+step·j) · (−1)^{alt·j}               */
/*   ln1p: (1,1,1,1)   atan: (1,2,1,1)                                 */
/* ------------------------------------------------------------------ */

static bigfrac_err_ty invpow_order_tail(size_t *out_n, bigfrac_ty *out_tail,
        const bigfrac_ty *r, const bigfrac_ty *eps, int start, int step,
        int den0) {
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

    bigfrac_ty q;
    bigfrac_ty one;
    bigfrac_ty one_minus_q;
    bigfrac_ty term;
    bigfrac_init(&q);
    bigfrac_init(&one);
    bigfrac_init(&one_minus_q);
    bigfrac_init(&term);

    /* q = r^step */
    err = bigfrac_copy(&q, r);
    for (int i = 1; i < step && err == BIGFRAC_OK_E; i++) {
        err = bigfrac_mul(&q, &q, r);
    }
    if (err == BIGFRAC_OK_E) {
        err = frac_u64(&one, 1U);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_sub(&one_minus_q, &one, &q);
        if (bigfrac_cmp(&one_minus_q, &zero) <= 0) {
            err = BIGFRAC_ERR_INVALID_E;     /* 需 r < 1 */
        }
    }
    /* term = r^start / den0（start ∈ {0,1}） */
    if (err == BIGFRAC_OK_E) {
        if (start == 0) {
            err = frac_u64(&term, 1U);
        } else {
            err = bigfrac_copy(&term, r);
        }
        if (err == BIGFRAC_OK_E) {
            bigfrac_ty den;
            bigfrac_init(&den);
            err = frac_u64(&den, (uint64_t)den0);
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_div(&term, &term, &den);
            }
            bigfrac_free(&den);
        }
    }

    for (size_t n = 0U; n <= NEX_SERIES_ORDER_CAP && err == BIGFRAC_OK_E; n++) {
        bigfrac_ty tail;
        bigfrac_init(&tail);
        err = bigfrac_div(&tail, &term, &one_minus_q);
        if (err == BIGFRAC_OK_E && bigfrac_cmp(&tail, eps) <= 0) {
            *out_n = n;
            if (out_tail != NULL) {
                err = bigfrac_copy(out_tail, &tail);
            }
            bigfrac_free(&tail);
            break;
        }
        bigfrac_free(&tail);
        /* term *= q·(den0+step·n)/(den0+step·(n+1)) */
        bigfrac_ty num;
        bigfrac_ty dn;
        bigfrac_init(&num);
        bigfrac_init(&dn);
        err = frac_u64(&dn, (uint64_t)(den0 + step * (int)(n + 1U)));
        if (err == BIGFRAC_OK_E) {
            err = frac_u64(&num, (uint64_t)(den0 + step * (int)n));
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_mul(&num, &num, &q);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_mul(&term, &term, &num);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_div(&term, &term, &dn);
        }
        bigfrac_free(&dn);
        bigfrac_free(&num);
        if (err != BIGFRAC_OK_E) {
            break;
        }
        if (n == NEX_SERIES_ORDER_CAP) {
            err = BIGFRAC_ERR_INVALID_E;     /* 超出上限 */
        }
    }

    bigfrac_free(&term);
    bigfrac_free(&one_minus_q);
    bigfrac_free(&one);
    bigfrac_free(&q);
    bigfrac_free(&zero);
    return err;
}



bigfrac_err_ty nex_series_ln1p_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps) {
    return invpow_order_tail(out_n, NULL, r, eps, 1, 1, 1);
}

bigfrac_err_ty nex_series_atan_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps) {
    return invpow_order_tail(out_n, NULL, r, eps, 1, 2, 1);
}

static bigfloat_err_ty invpow_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx, int start, int step, int den0, int alt) {
    if (out == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }
    size_t n = 0U;
    bigfloat_ty tail;
    bigfloat_init(&tail);
    bigfloat_err_ty err = invpow_order_fast(&n, &tail, z, eps, start, step,
            den0, ctx);
    if (err == BIGFLOAT_OK_E) {
        err = invpow_sum_float(out, err_out, z, &tail, n, start, step, den0,
                alt, ctx);
    }
    bigfloat_free(&tail);
    return err;
}



static bigdecimal_err_ty invpow_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx, int start,
        int step, int den0, int alt) {
    if (out == NULL || z == NULL || eps == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }
    size_t n = 0U;
    bigdecimal_ty tail;
    bigdecimal_init(&tail);
    bigdecimal_err_ty err = invpow_order_fast_dec(&n, &tail, z, eps, start,
            step, den0, ctx);
    if (err == BIGDECIMAL_OK_E) {
        err = invpow_sum_decimal(out, err_out, z, &tail, n, start, step,
                den0, alt, ctx);
    }
    bigdecimal_free(&tail);
    return err;
}



bigfloat_err_ty nex_series_ln1p_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx) {
    return invpow_eval_float(out, err_out, z, eps, ctx, 1, 1, 1, 1);
}

bigfloat_err_ty nex_series_atan_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx) {
    return invpow_eval_float(out, err_out, z, eps, ctx, 1, 2, 1, 1);
}

bigdecimal_err_ty nex_series_ln1p_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx) {
    return invpow_eval_decimal(out, err_out, z, eps, ctx, 1, 1, 1, 1);
}

bigdecimal_err_ty nex_series_atan_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx) {
    return invpow_eval_decimal(out, err_out, z, eps, ctx, 1, 2, 1, 1);
}
