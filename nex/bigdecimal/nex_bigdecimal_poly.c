#include "nex/bigdecimal/nex_bigdecimal.h"

/*
 * 多项式求值（秦九韶 / Horner，TODO §C1）。
 *
 * 每步 mul / add 按 ctx 正确舍入；整体是 Horner 递推结果，而非对精确
 * 多项式值的单次正确舍入。语义与别名/异常安全约定见
 * nex_bigint_bin_poly.c。
 */
bigdecimal_err_ty bigdecimal_poly_eval(bigdecimal_ty *out,
        const bigdecimal_ty *coeffs, size_t degree, const bigdecimal_ty *x,
        const bigdecimal_ctx_ty *ctx)
{
    if (out == NULL || coeffs == NULL || x == NULL || ctx == NULL) {
        return BIGDECIMAL_ERR_INVALID_E;
    }

    bigdecimal_ty acc;
    bigdecimal_err_ty err = bigdecimal_init(&acc);
    if (err != BIGDECIMAL_OK_E) {
        return err;
    }

    err = bigdecimal_copy(&acc, &coeffs[degree]);
    for (size_t k = degree; k > 0; k--) {
        if (err != BIGDECIMAL_OK_E) {
            break;
        }
        err = bigdecimal_mul(&acc, &acc, x, ctx);
        if (err != BIGDECIMAL_OK_E) {
            break;
        }
        err = bigdecimal_add(&acc, &acc, &coeffs[k - 1], ctx);
    }
    if (err == BIGDECIMAL_OK_E) {
        err = bigdecimal_copy(out, &acc);
    }

    bigdecimal_free(&acc);
    return err;
}
