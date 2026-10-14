#include "nex/bigfrac/nex_bigfrac.h"

/*
 * 多项式求值（秦九韶 / Horner，TODO §C1）：精确有理数，无舍入。
 * 语义与别名/异常安全约定见 nex_bigint_bin_poly.c。
 */
bigfrac_err_ty bigfrac_poly_eval(bigfrac_ty *out,
        const bigfrac_ty *coeffs, size_t degree, const bigfrac_ty *x)
{
    if (out == NULL || coeffs == NULL || x == NULL) {
        return BIGFRAC_ERR_INVALID_E;
    }

    bigfrac_ty acc;
    bigfrac_err_ty err = bigfrac_init(&acc);
    if (err != BIGFRAC_OK_E) {
        return err;
    }

    err = bigfrac_copy(&acc, &coeffs[degree]);
    for (size_t k = degree; k > 0; k--) {
        if (err != BIGFRAC_OK_E) {
            break;
        }
        err = bigfrac_mul(&acc, &acc, x);
        if (err != BIGFRAC_OK_E) {
            break;
        }
        err = bigfrac_add(&acc, &acc, &coeffs[k - 1]);
    }
    if (err == BIGFRAC_OK_E) {
        err = bigfrac_copy(out, &acc);
    }

    bigfrac_free(&acc);
    return err;
}
