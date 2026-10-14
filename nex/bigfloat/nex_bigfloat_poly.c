#include "nex/bigfloat/nex_bigfloat.h"

/*
 * 多项式求值（秦九韶 / Horner，TODO §C1）。
 *
 * 每步 mul / add 按 ctx 正确舍入；整体是 Horner 递推结果，而非对精确
 * 多项式值的单次正确舍入。语义与别名/异常安全约定见
 * nex_bigint_bin_poly.c。
 */
bigfloat_err_ty bigfloat_poly_eval(bigfloat_ty *out,
        const bigfloat_ty *coeffs, size_t degree, const bigfloat_ty *x,
        const bigfloat_ctx_ty *ctx)
{
    if (out == NULL || coeffs == NULL || x == NULL || ctx == NULL) {
        return BIGFLOAT_ERR_INVALID_E;
    }

    bigfloat_ty acc;
    bigfloat_err_ty err = bigfloat_init(&acc);
    if (err != BIGFLOAT_OK_E) {
        return err;
    }

    err = bigfloat_copy(&acc, &coeffs[degree]);
    for (size_t k = degree; k > 0; k--) {
        if (err != BIGFLOAT_OK_E) {
            break;
        }
        err = bigfloat_mul(&acc, &acc, x, ctx);
        if (err != BIGFLOAT_OK_E) {
            break;
        }
        err = bigfloat_add(&acc, &acc, &coeffs[k - 1], ctx);
    }
    if (err == BIGFLOAT_OK_E) {
        err = bigfloat_copy(out, &acc);
    }

    bigfloat_free(&acc);
    return err;
}
