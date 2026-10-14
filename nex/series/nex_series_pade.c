#include "nex/series/nex_series.h"

#include <stddef.h>
#include <stdlib.h>

#include "nex/convert/nex_convert.h"
#include "nex/nex_alloc.h"

/*
 * Padé 逼近 [m/n]（设计文档 §15.7，TODO §C3）。
 *
 * 给定幂级数 f(z) = Σ_{k≥0} c_k z^k（要求 val = 0），求 P（次数 ≤ m）与
 * Q（次数 ≤ n，Q(0)=1）使 f − P/Q = O(z^{m+n+1})。系数满足
 *   Σ_{j=1}^{n} q_j c_{m+1+i−j} = −c_{m+1+i},  i = 0..n−1
 * 与 p_k = Σ_{j=0}^{min(k,n)} q_j c_{k−j}。对 q 用 bigfrac 精确高斯消元求解。
 */

static bigfrac_err_ty pade_coeff(const bigfrac_series_ty *s, size_t k,
        bigfrac_ty *out) {
    bigfrac_err_ty err;
    if (s->val == 0 && k < s->len) {
        err = bigfrac_copy(out, &s->coeffs[k]);
    } else {
        err = bigfrac_from_str(out, "0", NULL);
    }
    return err;
}

static void frac_swap(bigfrac_ty *a, bigfrac_ty *b) {
    bigfrac_ty t = *a;
    *a = *b;
    *b = t;
}

bigfrac_err_ty nex_series_pade_frac(bigfrac_series_ty *num,
        bigfrac_series_ty *den, const bigfrac_series_ty *series, size_t m,
        size_t n) {
    if (num == NULL || den == NULL || series == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (series->val != 0) {
        return BIGFRAC_ERR_INVALID_E;   /* 仅支持 val = 0 的幂级数 */
    }

    bigfrac_err_ty err = BIGFRAC_OK_E;
    bigfrac_ty *A = NULL;
    bigfrac_ty *b = NULL;
    bigfrac_ty *q = NULL;

    if (n > 0U) {
        A = (bigfrac_ty *)nex_malloc(n * n * sizeof(bigfrac_ty));
        b = (bigfrac_ty *)nex_malloc(n * sizeof(bigfrac_ty));
        q = (bigfrac_ty *)nex_malloc(n * sizeof(bigfrac_ty));
        if (A == NULL || b == NULL || q == NULL) {
            free(A);
            free(b);
            free(q);
            return BIGFRAC_ERR_OOM_E;
        }
        for (size_t i = 0U; i < n * n; i++) {
            bigfrac_init(&A[i]);
        }
        for (size_t i = 0U; i < n; i++) {
            bigfrac_init(&b[i]);
            bigfrac_init(&q[i]);
        }

        /* 装配 Toeplitz 系统 */
        for (size_t i = 0U; i < n && err == BIGFRAC_OK_E; i++) {
            for (size_t j = 1U; j <= n && err == BIGFRAC_OK_E; j++) {
                size_t k = m + 1U + i - j;
                err = pade_coeff(series, k, &A[i * n + (j - 1U)]);
            }
            if (err == BIGFRAC_OK_E) {
                bigfrac_ty c;
                bigfrac_init(&c);
                err = pade_coeff(series, m + 1U + i, &c);
                if (err == BIGFRAC_OK_E) {
                    err = bigfrac_neg(&c);
                }
                if (err == BIGFRAC_OK_E) {
                    err = bigfrac_copy(&b[i], &c);
                }
                bigfrac_free(&c);
            }
        }

        /* 高斯消元（首非零主元；精确运算无需选主） */
        for (size_t col = 0U; col < n && err == BIGFRAC_OK_E; col++) {
            size_t piv = col;
            bigfrac_ty zero;
            bigfrac_init(&zero);
            while (piv < n && bigfrac_cmp(&A[piv * n + col], &zero) == 0) {
                piv++;
            }
            bigfrac_free(&zero);
            if (piv == n) {
                err = BIGFRAC_ERR_INVALID_E;   /* 奇异 */
                break;
            }
            if (piv != col) {
                for (size_t j = 0U; j < n; j++) {
                    frac_swap(&A[col * n + j], &A[piv * n + j]);
                }
                frac_swap(&b[col], &b[piv]);
            }
            for (size_t row = col + 1U; row < n && err == BIGFRAC_OK_E; row++) {
                bigfrac_ty factor;
                bigfrac_init(&factor);
                err = bigfrac_div(&factor, &A[row * n + col],
                                  &A[col * n + col]);
                for (size_t j = col; j < n && err == BIGFRAC_OK_E; j++) {
                    bigfrac_ty t;
                    bigfrac_init(&t);
                    err = bigfrac_mul(&t, &factor, &A[col * n + j]);
                    if (err == BIGFRAC_OK_E) {
                        err = bigfrac_sub(&A[row * n + j], &A[row * n + j], &t);
                    }
                    bigfrac_free(&t);
                }
                if (err == BIGFRAC_OK_E) {
                    bigfrac_ty t;
                    bigfrac_init(&t);
                    err = bigfrac_mul(&t, &factor, &b[col]);
                    if (err == BIGFRAC_OK_E) {
                        err = bigfrac_sub(&b[row], &b[row], &t);
                    }
                    bigfrac_free(&t);
                }
                bigfrac_free(&factor);
            }
        }

        /* 回代 */
        for (size_t ii = n; ii-- > 0U && err == BIGFRAC_OK_E; ) {
            bigfrac_ty sum;
            bigfrac_init(&sum);
            err = bigfrac_copy(&sum, &b[ii]);
            for (size_t j = ii + 1U; j < n && err == BIGFRAC_OK_E; j++) {
                bigfrac_ty t;
                bigfrac_init(&t);
                err = bigfrac_mul(&t, &A[ii * n + j], &q[j]);
                if (err == BIGFRAC_OK_E) {
                    err = bigfrac_sub(&sum, &sum, &t);
                }
                bigfrac_free(&t);
            }
            if (err == BIGFRAC_OK_E) {
                err = bigfrac_div(&q[ii], &sum, &A[ii * n + ii]);
            }
            bigfrac_free(&sum);
        }
    }

    /* 组装分母 Q = 1 + q_1 z + … + q_n z^n */
    bigfrac_ty *denc = NULL;
    bigfrac_ty *numc = NULL;
    if (err == BIGFRAC_OK_E) {
        denc = (bigfrac_ty *)nex_malloc((n + 1U) * sizeof(bigfrac_ty));
        numc = (bigfrac_ty *)nex_malloc((m + 1U) * sizeof(bigfrac_ty));
        if (denc == NULL || numc == NULL) {
            err = BIGFRAC_ERR_OOM_E;
        }
    }
    if (err == BIGFRAC_OK_E) {
        for (size_t i = 0U; i <= n; i++) {
            bigfrac_init(&denc[i]);
        }
        for (size_t i = 0U; i <= m; i++) {
            bigfrac_init(&numc[i]);
        }
        err = bigfrac_from_str(&denc[0], "1", NULL);
        for (size_t j = 1U; j <= n && err == BIGFRAC_OK_E; j++) {
            err = bigfrac_copy(&denc[j], &q[j - 1U]);
        }
        /* p_k = Σ_{j=0}^{min(k,n)} q_j c_{k−j}，q_0 = 1 */
        for (size_t k = 0U; k <= m && err == BIGFRAC_OK_E; k++) {
            err = pade_coeff(series, k, &numc[k]);
            size_t jmax = (k < n) ? k : n;
            for (size_t j = 1U; j <= jmax && err == BIGFRAC_OK_E; j++) {
                bigfrac_ty ck;
                bigfrac_ty t;
                bigfrac_init(&ck);
                bigfrac_init(&t);
                err = pade_coeff(series, k - j, &ck);
                if (err == BIGFRAC_OK_E) {
                    err = bigfrac_mul(&t, &q[j - 1U], &ck);
                }
                if (err == BIGFRAC_OK_E) {
                    err = bigfrac_add(&numc[k], &numc[k], &t);
                }
                bigfrac_free(&t);
                bigfrac_free(&ck);
            }
        }
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_from_array(den, 0, denc, n + 1U);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_from_array(num, 0, numc, m + 1U);
    }

    if (numc != NULL) {
        for (size_t i = 0U; i <= m; i++) {
            bigfrac_free(&numc[i]);
        }
    }
    if (denc != NULL) {
        for (size_t i = 0U; i <= n; i++) {
            bigfrac_free(&denc[i]);
        }
    }
    free(numc);
    free(denc);
    if (q != NULL) {
        for (size_t i = 0U; i < n; i++) {
            bigfrac_free(&q[i]);
        }
        for (size_t i = 0U; i < n * n; i++) {
            bigfrac_free(&A[i]);
        }
        for (size_t i = 0U; i < n; i++) {
            bigfrac_free(&b[i]);
        }
    }
    free(q);
    free(b);
    free(A);
    return err;
}

/* ------------------------------------------------------------------ */
/* Padé 有理逼近求值（精确部分和 + 终值一次舍入）                      */
/* ------------------------------------------------------------------ */

bigfrac_err_ty nex_series_pade_eval_frac(bigfrac_ty *out,
        const bigfrac_series_ty *num, const bigfrac_series_ty *den,
        const bigfrac_ty *z) {
    if (out == NULL || num == NULL || den == NULL || z == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    bigfrac_ty P;
    bigfrac_ty Q;
    bigfrac_ty zero;
    bigfrac_init(&P);
    bigfrac_init(&Q);
    bigfrac_init(&zero);
    bigfrac_err_ty err = bigfrac_series_eval(&P, num, z);
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_eval(&Q, den, z);
    }
    if (err == BIGFRAC_OK_E && bigfrac_cmp(&Q, &zero) == 0) {
        err = BIGFRAC_ERR_DIV_ZERO_E;
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_div(out, &P, &Q);
    }
    bigfrac_free(&zero);
    bigfrac_free(&Q);
    bigfrac_free(&P);
    return err;
}

static bigfloat_err_ty pade_map_float(bigfrac_err_ty err) {
    return (err == BIGFRAC_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                      : BIGFLOAT_ERR_INVALID_E;
}

bigfloat_err_ty nex_series_pade_eval_float(bigfloat_ty *out,
        const bigfrac_series_ty *num, const bigfrac_series_ty *den,
        const bigfloat_ty *z, const bigfloat_ctx_ty *ctx) {
    if (out == NULL || num == NULL || den == NULL || z == NULL || ctx == NULL) {
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
    bigfrac_err_ty err = nex_series_pade_eval_frac(&r, num, den, &zf);
    bigfloat_err_ty result = BIGFLOAT_OK_E;
    if (err != BIGFRAC_OK_E) {
        result = pade_map_float(err);
    } else {
        conv = nex_convert_frac_to_float(out, &r, ctx);
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGFLOAT_ERR_OOM_E
                                                     : BIGFLOAT_ERR_INVALID_E;
        }
    }
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}

static bigdecimal_err_ty pade_map_dec(bigfrac_err_ty err) {
    return (err == BIGFRAC_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                      : BIGDECIMAL_ERR_INVALID_E;
}

bigdecimal_err_ty nex_series_pade_eval_decimal(bigdecimal_ty *out,
        const bigfrac_series_ty *num, const bigfrac_series_ty *den,
        const bigdecimal_ty *z, const bigdecimal_ctx_ty *ctx) {
    if (out == NULL || num == NULL || den == NULL || z == NULL || ctx == NULL) {
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
    bigfrac_err_ty err = nex_series_pade_eval_frac(&r, num, den, &zf);
    bigdecimal_err_ty result = BIGDECIMAL_OK_E;
    if (err != BIGFRAC_OK_E) {
        result = pade_map_dec(err);
    } else {
        conv = nex_convert_frac_to_decimal(out, &r, ctx);
        if (conv != NEX_CONVERT_OK_E) {
            result = (conv == NEX_CONVERT_ERR_OOM_E) ? BIGDECIMAL_ERR_OOM_E
                                                     : BIGDECIMAL_ERR_INVALID_E;
        }
    }
    bigfrac_free(&r);
    bigfrac_free(&zf);
    return result;
}
