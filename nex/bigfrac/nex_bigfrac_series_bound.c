#include "nex/bigfrac/nex_bigfrac.h"

#include <stddef.h>
#include <stdint.h>

/*
 * 几何型全局尾界与自适应截断阶（路线 A：精确系数 + 全局尾界，design §15.6）。
 *
 * 若 |a_k| ≤ A·B^k（∀k）、|z| ≤ r 且 q = B·r < 1，则
 *   Σ_{k≥n} a_k z^k ≤ A·q^n / (1 − q)。
 * 据此自适应选阶：最小 n 使 A·q^n ≤ eps·(1 − q)。
 */

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

bigfrac_err_ty bigfrac_series_tail_geometric(bigfrac_ty *out,
        const bigfrac_ty *A, const bigfrac_ty *B, const bigfrac_ty *r,
        size_t n) {
    if (out == NULL || A == NULL || B == NULL || r == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty zero;
    bigfrac_ty one;
    bigfrac_ty q;
    bigfrac_ty denom;
    bigfrac_ty pow;
    bigfrac_ty num;
    bigfrac_init(&zero);
    bigfrac_init(&one);
    bigfrac_init(&q);
    bigfrac_init(&denom);
    bigfrac_init(&pow);
    bigfrac_init(&num);

    bigfrac_err_ty err = BIGFRAC_OK_E;
    if (bigfrac_cmp(A, &zero) < 0 || bigfrac_cmp(B, &zero) < 0 ||
            bigfrac_cmp(r, &zero) < 0) {
        err = BIGFRAC_ERR_INVALID_E;
    }
    if (err == BIGFRAC_OK_E) {
        err = frac_u64(&one, 1U);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_mul(&q, B, r);            /* q = B·r */
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_sub(&denom, &one, &q);    /* 1 − q */
        if (err == BIGFRAC_OK_E && bigfrac_cmp(&denom, &zero) <= 0) {
            err = BIGFRAC_ERR_INVALID_E;        /* 需 q < 1 */
        }
    }
    if (err == BIGFRAC_OK_E) {
        err = frac_u64(&pow, 1U);               /* q^0 */
    }
    for (size_t i = 0U; i < n && err == BIGFRAC_OK_E; i++) {
        err = bigfrac_mul(&pow, &pow, &q);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_mul(&num, A, &pow);       /* A·q^n */
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_div(&num, &num, &denom);  /* / (1 − q) */
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(out, &num);
    }

    bigfrac_free(&num);
    bigfrac_free(&pow);
    bigfrac_free(&denom);
    bigfrac_free(&q);
    bigfrac_free(&one);
    bigfrac_free(&zero);
    return err;
}

bigfrac_err_ty bigfrac_series_order_geometric(size_t *out,
        const bigfrac_ty *A, const bigfrac_ty *B, const bigfrac_ty *r,
        const bigfrac_ty *eps) {
    if (out == NULL || A == NULL || B == NULL || r == NULL || eps == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty zero;
    bigfrac_ty one;
    bigfrac_ty q;
    bigfrac_ty one_minus_q;
    bigfrac_ty rhs;
    bigfrac_ty lhs;
    bigfrac_init(&zero);
    bigfrac_init(&one);
    bigfrac_init(&q);
    bigfrac_init(&one_minus_q);
    bigfrac_init(&rhs);
    bigfrac_init(&lhs);

    bigfrac_err_ty err = BIGFRAC_OK_E;
    if (bigfrac_cmp(A, &zero) < 0 || bigfrac_cmp(B, &zero) < 0 ||
            bigfrac_cmp(r, &zero) < 0 || bigfrac_cmp(eps, &zero) < 0) {
        err = BIGFRAC_ERR_INVALID_E;
    }
    if (err == BIGFRAC_OK_E) {
        err = frac_u64(&one, 1U);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_mul(&q, B, r);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_sub(&one_minus_q, &one, &q);
        if (err == BIGFRAC_OK_E && bigfrac_cmp(&one_minus_q, &zero) <= 0) {
            err = BIGFRAC_ERR_INVALID_E;        /* 需 q < 1 */
        }
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_mul(&rhs, eps, &one_minus_q);   /* eps·(1 − q) */
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(&lhs, A);            /* A·q^0 */
    }

    size_t n = 0U;
    const size_t cap = 1000000U;
    while (err == BIGFRAC_OK_E && bigfrac_cmp(&lhs, &rhs) > 0) {
        if (n >= cap) {
            err = BIGFRAC_ERR_INVALID_E;        /* q 极接近 1，超出迭代上限 */
            break;
        }
        err = bigfrac_mul(&lhs, &lhs, &q);
        n++;
    }
    if (err == BIGFRAC_OK_E) {
        *out = n;
    }

    bigfrac_free(&lhs);
    bigfrac_free(&rhs);
    bigfrac_free(&one_minus_q);
    bigfrac_free(&q);
    bigfrac_free(&one);
    bigfrac_free(&zero);
    return err;
}

bigfrac_err_ty bigfrac_series_tail_ratio(bigfrac_ty *out,
        const bigfrac_ty *first_omitted, const bigfrac_ty *q) {
    if (out == NULL || first_omitted == NULL || q == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty zero;
    bigfrac_ty one;
    bigfrac_ty one_minus_q;
    bigfrac_init(&zero);
    bigfrac_init(&one);
    bigfrac_init(&one_minus_q);

    bigfrac_err_ty err = BIGFRAC_OK_E;
    if (bigfrac_cmp(first_omitted, &zero) < 0 || bigfrac_cmp(q, &zero) < 0) {
        err = BIGFRAC_ERR_INVALID_E;
    }
    if (err == BIGFRAC_OK_E) {
        err = frac_u64(&one, 1U);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_sub(&one_minus_q, &one, q);
        if (err == BIGFRAC_OK_E && bigfrac_cmp(&one_minus_q, &zero) <= 0) {
            err = BIGFRAC_ERR_INVALID_E;        /* 需 q < 1 */
        }
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_div(out, first_omitted, &one_minus_q);
    }

    bigfrac_free(&one_minus_q);
    bigfrac_free(&one);
    bigfrac_free(&zero);
    return err;
}
