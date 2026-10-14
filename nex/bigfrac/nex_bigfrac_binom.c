#include "nex/bigfrac/nex_bigfrac.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "nex/nex_alloc.h"

/*
 * 广义二项式系数与二项式级数（设计文档 §15.5，TODO §C2）。
 *
 *   C(alpha,k) = alpha(alpha−1)···(alpha−k+1)/k!
 *   (1+z)^alpha = Σ_{k≥0} C(alpha,k) z^k
 *
 * 均为 bigfrac 精确运算。alpha 为非负整数 n 时 C(n,k) 在 k > n 处为 0，
 * 级数为 n 次多项式。
 */

static bigfrac_err_ty frac_from_u64(bigfrac_ty *out, uint64_t v) {
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

bigfrac_err_ty bigfrac_binom(bigfrac_ty *out, const bigfrac_ty *alpha,
        size_t k) {
    if (out == NULL || alpha == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty acc;
    bigfrac_ty term;
    bigfrac_ty idx;
    bigfrac_init(&acc);
    bigfrac_init(&term);
    bigfrac_init(&idx);

    /* 递推：C(alpha,0) = 1；C(alpha,j+1) = C(alpha,j)·(alpha−j)/(j+1) */
    bigfrac_err_ty err = frac_from_u64(&acc, 1U);
    for (size_t j = 0U; j < k && err == BIGFRAC_OK_E; j++) {
        err = frac_from_u64(&idx, (uint64_t)j);
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_sub(&term, alpha, &idx);
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_mul(&acc, &acc, &term);
        }
        if (err == BIGFRAC_OK_E) {
            err = frac_from_u64(&idx, (uint64_t)(j + 1U));
        }
        if (err == BIGFRAC_OK_E) {
            err = bigfrac_div(&acc, &acc, &idx);
        }
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(out, &acc);
    }

    bigfrac_free(&idx);
    bigfrac_free(&term);
    bigfrac_free(&acc);
    return err;
}

bigfrac_err_ty bigfrac_series_binom(bigfrac_series_ty *dst,
        const bigfrac_ty *alpha, size_t order) {
    if (dst == NULL || alpha == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }
    if (order == 0U) {
        return bigfrac_series_from_array(dst, 0, NULL, 0U);
    }

    bigfrac_ty *coeffs = (bigfrac_ty *)nex_malloc(order * sizeof(bigfrac_ty));
    if (coeffs == NULL) {
        return BIGFRAC_ERR_OOM_E;
    }
    for (size_t k = 0U; k < order; k++) {
        bigfrac_init(&coeffs[k]);
    }

    bigfrac_err_ty err = BIGFRAC_OK_E;
    for (size_t k = 0U; k < order && err == BIGFRAC_OK_E; k++) {
        err = bigfrac_binom(&coeffs[k], alpha, k);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_series_from_array(dst, 0, coeffs, order);
    }

    for (size_t k = 0U; k < order; k++) {
        bigfrac_free(&coeffs[k]);
    }
    free(coeffs);
    return err;
}
