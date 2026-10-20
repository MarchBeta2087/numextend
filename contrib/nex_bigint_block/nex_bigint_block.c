/*
 * nex_bigint_block.c：contrib 层递归分块 NTT 乘法（P1b，issue #98）。
 *
 * 递归折半直到 max_len ≤ 2^26（此时单次直接 NTT 的 s ≤ 2^27）：
 *   a = a0 + a1·B^L, b = b0 + b1·B^L
 *   a·b = c0 + (sa·sb − c0 − c2)·B^L + c2·B^(2L)
 * 三个子积递归；10^9 位（半积 s≈1.04e8）只需一层；10^10 位约 4 层。
 */

#include "nex_bigint_block.h"

#include "nex/bigint/nex_bigint_common.h"

#include <stdint.h>
#include <string.h>

/* 直接 NTT 的肢数和上限为 2^27，故单块 max_len ≤ 2^26 */
#define BLOCK_BASE_LIMBS (1U << 26U)
#define BLOCK_MAX_LIMBS (1U << 30U)

static bigint_err_ty make_part(bigint_bin_ty *dst,
        const bigint_bin_ty *src, size_t begin, size_t end)
{
    if (begin > src->len) {
        begin = src->len;
    }
    if (end > src->len) {
        end = src->len;
    }
    const size_t len = (end > begin) ? (end - begin) : 0U;
    (void)bigint_bin_init(dst);
    if (len == 0U) {
        return BIGINT_OK_E;
    }
    const bigint_err_ty err = bigint_bin_init_cap(dst, len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    memcpy(dst->limbs, src->limbs + begin, len * sizeof(uint32_t));
    dst->len = len;
    dst->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/* 幅值递归乘法：lhs/rhs 非零且为正 */
static bigint_err_ty block_mul_mag(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    if (max_len > BLOCK_MAX_LIMBS) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }
    if (max_len <= BLOCK_BASE_LIMBS) {
        /* AUTO：两个大块走 NTT；大数 × 小乘数会走 schoolbook，避免
         * 为单个小乘数启动 2^27 长度的 NTT。 */
        return bigint_bin_mul(dst, lhs, rhs);
    }

    const size_t half = (max_len + 1U) / 2U;
    if (half > SIZE_MAX / 64U) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }

    bigint_bin_ty a0;
    bigint_bin_ty a1;
    bigint_bin_ty b0;
    bigint_bin_ty b1;
    bigint_bin_ty sa;
    bigint_bin_ty sb;
    bigint_bin_ty c0;
    bigint_bin_ty c1;
    bigint_bin_ty c2;
    bigint_bin_ty res;
    bigint_bin_ty tmp;
    (void)bigint_bin_init(&a0);
    (void)bigint_bin_init(&a1);
    (void)bigint_bin_init(&b0);
    (void)bigint_bin_init(&b1);
    (void)bigint_bin_init(&sa);
    (void)bigint_bin_init(&sb);
    (void)bigint_bin_init(&c0);
    (void)bigint_bin_init(&c1);
    (void)bigint_bin_init(&c2);
    (void)bigint_bin_init(&res);
    (void)bigint_bin_init(&tmp);

    bigint_err_ty err = make_part(&a0, lhs, 0U, half);
    if (err == BIGINT_OK_E) {
        err = make_part(&a1, lhs, half, lhs->len);
    }
    if (err == BIGINT_OK_E) {
        err = make_part(&b0, rhs, 0U, half);
    }
    if (err == BIGINT_OK_E) {
        err = make_part(&b1, rhs, half, rhs->len);
    }
    if (err == BIGINT_OK_E) {
        err = block_mul_mag(&c0, &a0, &b0);
    }
    if (err == BIGINT_OK_E) {
        err = block_mul_mag(&c2, &a1, &b1);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&sa, &a0, &a1);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&sb, &b0, &b1);
    }
    if (err == BIGINT_OK_E) {
        err = block_mul_mag(&c1, &sa, &sb);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_sub(&c1, &c1, &c0);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_sub(&c1, &c1, &c2);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_shl(&tmp, &c2, 64U * half);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&res, &c0, &tmp);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_shl(&tmp, &c1, 32U * half);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&res, &res, &tmp);
    }
    if (err == BIGINT_OK_E) {
        res.sign = (res.len > 0U) ? BIGINT_SIGN_POS_E : BIGINT_SIGN_ZERO_E;
        bigint_bin_move(dst, &res);
    }

    bigint_bin_free(&tmp);
    bigint_bin_free(&res);
    bigint_bin_free(&c2);
    bigint_bin_free(&c1);
    bigint_bin_free(&c0);
    bigint_bin_free(&sb);
    bigint_bin_free(&sa);
    bigint_bin_free(&b1);
    bigint_bin_free(&b0);
    bigint_bin_free(&a1);
    bigint_bin_free(&a0);
    return err;
}

bigint_err_ty nex_block_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((lhs->len == 0U) || (rhs->len == 0U)) {
        return bigint_bin_from_u64(dst, 0U);
    }

    bigint_bin_ty tmp;
    (void)bigint_bin_init(&tmp);
    bigint_err_ty err = block_mul_mag(&tmp, lhs, rhs);
    if (err == BIGINT_OK_E) {
        if (tmp.len > 0U) {
            const bool neg = (lhs->sign == BIGINT_SIGN_NEG_E)
                    != (rhs->sign == BIGINT_SIGN_NEG_E);
            tmp.sign = neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
        }
        bigint_bin_move(dst, &tmp);
    } else {
        bigint_bin_free(&tmp);
    }
    return err;
}
