#include "nex/bigint/bin/nex_bigint_bin.h"

/*
 * 多项式求值（秦九韶 / Horner，TODO §C1）。
 *
 * out = Σ_{i=0}^{degree} coeffs[i]·x^i，由高次到低次递推：
 *   acc = coeffs[degree]
 *   acc = acc·x + coeffs[k]   (k = degree-1 … 0)
 *
 * 先算入局部 acc，最后 copy 到 out：因此 out 允许与 x / coeffs 别名，
 * 且 OOM 时 out 保持调用前状态（copy 的强异常安全）。
 */
bigint_err_ty bigint_bin_poly_eval(bigint_bin_ty *out,
        const bigint_bin_ty *coeffs, size_t degree, const bigint_bin_ty *x)
{
    if (out == NULL || coeffs == NULL || x == NULL) {
        return BIGINT_ERR_INVALID_E;
    }

    bigint_bin_ty acc;
    bigint_err_ty err = bigint_bin_init(&acc);
    if (err != BIGINT_OK_E) {
        return err;
    }

    err = bigint_bin_copy(&acc, &coeffs[degree]);
    for (size_t k = degree; k > 0; k--) {
        if (err != BIGINT_OK_E) {
            break;
        }
        err = bigint_bin_mul(&acc, &acc, x);
        if (err != BIGINT_OK_E) {
            break;
        }
        err = bigint_bin_add(&acc, &acc, &coeffs[k - 1]);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_copy(out, &acc);
    }

    bigint_bin_free(&acc);
    return err;
}
