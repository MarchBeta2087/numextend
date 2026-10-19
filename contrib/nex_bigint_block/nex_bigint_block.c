/*
 * nex_bigint_block.c：contrib 层分块 NTT 乘法（P1，issue #98）。
 *
 * 三个半长子积仍用核心 bigint_bin_mul_ex（强制多模数 CRT NTT），
 * 再按 B^L / B^(2L) 合并。这样 10^9 位（半积 s≈1.04e8 ≤ 2^27）
 * 可落在核心 NTT 上限内；更大规模需要多级分块或 SSA。
 */

#include "nex_bigint_block.h"

#include "nex/bigint/nex_bigint_common.h"

#include <stdint.h>
#include <string.h>

/* 与核心 NEX_MUL_NTT_MAX_SUM 一致 */
#define BLOCK_NTT_MAX_SUM (1U << 27U)

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

bigint_err_ty nex_block_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return BIGINT_ERR_INVALID_E;
    }
    if ((lhs->len == 0U) || (rhs->len == 0U)) {
        return bigint_bin_from_u64(dst, 0U);
    }

    const size_t max_len = (lhs->len > rhs->len) ? lhs->len : rhs->len;
    const size_t half = (max_len + 1U) / 2U;
    if ((half > (size_t)1U << 26U) || (2U * half > BLOCK_NTT_MAX_SUM)) {
        return BIGINT_ERR_UNSUPPORTED_E;
    }
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

    bigint_mul_method_ty method;
    method.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
    method.params.multi_moduli_crt_ntt.mod_count = 0U;

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
        err = bigint_bin_mul_ex(&c0, &a0, &b0, &method);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul_ex(&c2, &a1, &b1, &method);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&sa, &a0, &a1);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_add(&sb, &b0, &b1);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_mul_ex(&c1, &sa, &sb, &method);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_sub(&c1, &c1, &c0);
    }
    if (err == BIGINT_OK_E) {
        err = bigint_bin_sub(&c1, &c1, &c2);
    }
    if (err == BIGINT_OK_E) {
        /* res = c0 + c1·B^half + c2·B^(2·half) */
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
        if (res.len > 0U) {
            const bool neg = (lhs->sign == BIGINT_SIGN_NEG_E)
                    != (rhs->sign == BIGINT_SIGN_NEG_E);
            res.sign = neg ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
        }
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
