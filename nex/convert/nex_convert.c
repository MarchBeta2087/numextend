/*
 * nex_convert.c：类型间转换单元（设计文档 §10 转换矩阵）。
 *
 * 实现要点（§10 原则）：
 *   - 精确互转直接组装（num/den、shl/mul_pow10、bigint_conv bin↔dec）；
 *   - 逆向精确转换（浮点 → 整数）仅接受整数值，否则 INVALID，不截断；
 *   - 有损转换（→ bigfloat / bigdecimal）经精确中间值（有理数）带保护位
 *     整数除法 + 一次 round_pack 舍入（无双重舍入）：frac → float / decimal
 *     镜像各自 div 的移位公式；float ↔ decimal 经精确有理数互转；
 *   - 极端指数用对数估计预检上/下溢，避免构造 10^18 级巨数（否则
 *     mul_pow10 / pow 直接 OOM 而非按语义产生 ±∞ / flush）。
 *
 * 本单元为库内部胶水层，使用目标模块的内部 round_pack（内部头文件标注
 * "nex 库内部使用"），集中实现矩阵转换，避免给各模块注入跨支线依赖。
 */

#include "nex/convert/nex_convert.h"
#include "nex/nex_alloc.h"

#include "nex/bigfloat/nex_bigfloat_internal.h"
#include "nex/bigdecimal/nex_bigdecimal_internal.h"

#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* 内部辅助                                                             */
/* ------------------------------------------------------------------ */

static nex_convert_err_ty map_bi_err(bigint_err_ty err)
{
    if (err == BIGINT_OK_E) {
        return NEX_CONVERT_OK_E;
    }
    if (err == BIGINT_ERR_OOM_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    return NEX_CONVERT_ERR_INVALID_E;
}

static nex_convert_err_ty map_frac_err(bigfrac_err_ty err)
{
    if (err == BIGFRAC_OK_E) {
        return NEX_CONVERT_OK_E;
    }
    if (err == BIGFRAC_ERR_OOM_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    return NEX_CONVERT_ERR_INVALID_E;
}

/* bigint_bin 是否为 ±1（幅值为 1） */
static bool bin_is_one(const bigint_bin_ty *v)
{
    return (bigint_bin_bit_len(v) == 1U) && bigint_bin_bit_test(v, 0U);
}

/* ------------------------------------------------------------------ */
/* 精确互转（§10）                                                     */
/* ------------------------------------------------------------------ */

nex_convert_err_ty nex_convert_bin_to_frac(bigfrac_ty *dst,
        const bigint_bin_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    bigint_bin_ty den;
    bigint_err_ty berr = bigint_bin_init(&den);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_bin_from_u64(&den, 1U);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&den);
        return map_bi_err(berr);
    }
    const bigfrac_err_ty ferr = bigfrac_from_ints(dst, src, &den);
    bigint_bin_free(&den);
    return map_frac_err(ferr);
}

nex_convert_err_ty nex_convert_dec_to_frac(bigfrac_ty *dst,
        const bigint_dec_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    bigint_bin_ty bin;
    bigint_err_ty berr = bigint_bin_init(&bin);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_conv_dec_to_bin(&bin, src);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&bin);
        return map_bi_err(berr);
    }
    const nex_convert_err_ty rc = nex_convert_bin_to_frac(dst, &bin);
    bigint_bin_free(&bin);
    return rc;
}

nex_convert_err_ty nex_convert_frac_to_bin(bigint_bin_ty *dst,
        const bigfrac_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (!bin_is_one(&src->den)) {
        return NEX_CONVERT_ERR_INVALID_E;  // 非整数，不截断
    }
    return map_bi_err(bigint_bin_copy(dst, &src->num));
}

nex_convert_err_ty nex_convert_frac_to_dec(bigint_dec_ty *dst,
        const bigfrac_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (!bin_is_one(&src->den)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    return map_bi_err(bigint_conv_bin_to_dec(dst, &src->num));
}

nex_convert_err_ty nex_convert_float_to_frac(bigfrac_ty *dst,
        const bigfloat_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(src) || bigfloat_is_inf(src)) {
        return NEX_CONVERT_ERR_INVALID_E;  // 特殊值不是有限有理数
    }
    if (bigfloat_is_zero(src)) {
        bigint_bin_ty zero;
        bigint_bin_ty one;
        bigint_err_ty berr = bigint_bin_init(&zero);
        if (berr != BIGINT_OK_E) {
            return NEX_CONVERT_ERR_OOM_E;
        }
        berr = bigint_bin_init(&one);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&zero);
            return NEX_CONVERT_ERR_OOM_E;
        }
        bigfrac_err_ty ferr = BIGFRAC_ERR_INVALID_E;
        berr = bigint_bin_from_u64(&one, 1U);
        if (berr == BIGINT_OK_E) {
            ferr = bigfrac_from_ints(dst, &zero, &one);
        }
        bigint_bin_free(&zero);
        bigint_bin_free(&one);
        return map_frac_err(ferr);
    }
    /* value = mant × 2^exp：exp ≥ 0 → num = mant<<exp, den = 1；
     * exp < 0 → num = mant, den = 2^|exp| */
    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_err_ty berr = bigint_bin_init(&num);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_bin_init(&den);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&num);
        return NEX_CONVERT_ERR_OOM_E;
    }
    if (src->exp >= 0) {
        berr = bigint_bin_copy(&num, &src->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_shl(&num, &num, (size_t)src->exp);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_from_u64(&den, 1U);
        }
    } else {
        berr = bigint_bin_copy(&num, &src->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_from_u64(&den, 1U);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_shl(&den, &den, (size_t)(-(src->exp)));
        }
    }
    bigfrac_err_ty ferr = BIGFRAC_ERR_INVALID_E;
    if (berr == BIGINT_OK_E) {
        if (src->flag == BIGFLOAT_NEG_E) {
            bigint_bin_neg(&num);  // 符号由分子承担（bigfrac 约定）
        }
        ferr = bigfrac_from_ints(dst, &num, &den);
    }
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    return map_frac_err(ferr);
}

nex_convert_err_ty nex_convert_decimal_to_frac(bigfrac_ty *dst,
        const bigdecimal_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(src) || bigdecimal_is_inf(src)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigdecimal_is_zero(src)) {
        bigint_bin_ty zero;
        bigint_bin_ty one;
        bigint_err_ty berr = bigint_bin_init(&zero);
        if (berr != BIGINT_OK_E) {
            return NEX_CONVERT_ERR_OOM_E;
        }
        berr = bigint_bin_init(&one);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&zero);
            return NEX_CONVERT_ERR_OOM_E;
        }
        bigfrac_err_ty ferr = BIGFRAC_ERR_INVALID_E;
        berr = bigint_bin_from_u64(&one, 1U);
        if (berr == BIGINT_OK_E) {
            ferr = bigfrac_from_ints(dst, &zero, &one);
        }
        bigint_bin_free(&zero);
        bigint_bin_free(&one);
        return map_frac_err(ferr);
    }
    /* value = mant_dec × 10^exp：exp ≥ 0 → num = mant×10^exp, den = 1；
     * exp < 0 → num = mant, den = 10^|exp| */
    bigint_dec_ty num_dec;
    bigint_err_ty berr = bigint_dec_init(&num_dec);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_dec_copy(&num_dec, &src->mant);
    if (berr == BIGINT_OK_E) {
        if (src->exp >= 0) {
            berr = bigint_dec_mul_pow10(&num_dec, (size_t)src->exp);
        }
    }
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&num_dec);
        return map_bi_err(berr);
    }
    bigint_bin_ty num;
    bigint_err_ty berr2 = bigint_bin_init(&num);
    if (berr2 != BIGINT_OK_E) {
        bigint_dec_free(&num_dec);
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr2 = bigint_conv_dec_to_bin(&num, &num_dec);
    bigint_dec_free(&num_dec);
    if (berr2 != BIGINT_OK_E) {
        bigint_bin_free(&num);
        return map_bi_err(berr2);
    }
    bigint_bin_ty den;
    berr2 = bigint_bin_init(&den);
    if (berr2 != BIGINT_OK_E) {
        bigint_bin_free(&num);
        return NEX_CONVERT_ERR_OOM_E;
    }
    if (src->exp >= 0) {
        berr2 = bigint_bin_from_u64(&den, 1U);
    } else {
        /* den = 10^|exp|（bigint_bin 十进制幂） */
        bigint_bin_ty ten;
        berr2 = bigint_bin_init(&ten);
        if (berr2 == BIGINT_OK_E) {
            berr2 = bigint_bin_from_u64(&ten, 10U);
        }
        if (berr2 == BIGINT_OK_E) {
            berr2 = bigint_bin_pow(&den, &ten, (uint64_t)(-(src->exp)));
        }
        bigint_bin_free(&ten);
    }
    bigfrac_err_ty ferr = BIGFRAC_ERR_INVALID_E;
    if (berr2 == BIGINT_OK_E) {
        if (src->flag == BIGDECIMAL_NEG_E) {
            bigint_bin_neg(&num);
        }
        ferr = bigfrac_from_ints(dst, &num, &den);
    }
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    return map_frac_err(ferr);
}

/* ------------------------------------------------------------------ */
/* 逆向精确转换（§10：仅整数值且有限）                                 */
/* ------------------------------------------------------------------ */

nex_convert_err_ty nex_convert_float_to_bin(bigint_bin_ty *dst,
        const bigfloat_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigfloat_is_nan(src) || bigfloat_is_inf(src)) {
        return NEX_CONVERT_ERR_INVALID_E;  // NaN/∞ 非有限整数
    }
    if (bigfloat_is_zero(src)) {
        /* ±0 → 整数 0（符号丢失，按整数语义） */
        const bigint_err_ty zer = bigint_bin_from_u64(dst, 0U);
        return map_bi_err(zer);
    }
    const size_t bl = bigint_bin_bit_len(&src->mant);
    bigint_bin_ty mag;
    if (src->exp >= 0) {
        bigint_err_ty berr = bigint_bin_init(&mag);
        if (berr != BIGINT_OK_E) {
            return NEX_CONVERT_ERR_OOM_E;
        }
        berr = bigint_bin_copy(&mag, &src->mant);
        if (berr == BIGINT_OK_E && src->exp > 0) {
            berr = bigint_bin_shl(&mag, &mag, (size_t)src->exp);
        }
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&mag);
            return map_bi_err(berr);
        }
    } else {
        /* exp < 0：整数 ⟺ 尾数低位 |exp| 位全零（归一化尾数可为任意位长） */
        const size_t r = (size_t)(-(src->exp));
        if (r >= bl) {
            return NEX_CONVERT_ERR_INVALID_E;  // 值 < 2，非零则非整数
        }
        /* 低位逐位检查（尾数可超 64 位，不能用 to_u64 掩码） */
        for (size_t i = 0U; i < r; i++) {
            if (bigint_bin_bit_test(&src->mant, i)) {
                return NEX_CONVERT_ERR_INVALID_E;  // 低位非零 → 非整数
            }
        }
        bigint_err_ty berr = bigint_bin_init(&mag);
        if (berr != BIGINT_OK_E) {
            return NEX_CONVERT_ERR_OOM_E;
        }
        berr = bigint_bin_copy(&mag, &src->mant);
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_shr(&mag, &mag, r);
        }
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&mag);
            return map_bi_err(berr);
        }
    }
    if (src->flag == BIGFLOAT_NEG_E) {
        bigint_bin_neg(&mag);
    }
    bigint_bin_move(dst, &mag);
    return NEX_CONVERT_OK_E;
}

nex_convert_err_ty nex_convert_float_to_dec(bigint_dec_ty *dst,
        const bigfloat_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    bigint_bin_ty bin;
    bigint_err_ty berr = bigint_bin_init(&bin);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    const nex_convert_err_ty rc = nex_convert_float_to_bin(&bin, src);
    if (rc != NEX_CONVERT_OK_E) {
        bigint_bin_free(&bin);
        return rc;
    }
    berr = bigint_conv_bin_to_dec(dst, &bin);
    bigint_bin_free(&bin);
    return map_bi_err(berr);
}

nex_convert_err_ty nex_convert_decimal_to_bin(bigint_bin_ty *dst,
        const bigdecimal_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(src) || bigdecimal_is_inf(src)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigdecimal_is_zero(src)) {
        const bigint_err_ty zer = bigint_bin_from_u64(dst, 0U);
        return map_bi_err(zer);
    }
    if (src->exp < 0) {
        return NEX_CONVERT_ERR_INVALID_E;  // 负数指数非整数
    }
    bigint_dec_ty work;
    bigint_err_ty berr = bigint_dec_init(&work);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_dec_copy(&work, &src->mant);
    if (berr == BIGINT_OK_E && src->exp > 0) {
        berr = bigint_dec_mul_pow10(&work, (size_t)src->exp);
    }
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&work);
        return map_bi_err(berr);
    }
    bigint_bin_ty mag;
    berr = bigint_bin_init(&mag);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&work);
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_conv_dec_to_bin(&mag, &work);
    bigint_dec_free(&work);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&mag);
        return map_bi_err(berr);
    }
    if (src->flag == BIGDECIMAL_NEG_E) {
        bigint_bin_neg(&mag);
    }
    bigint_bin_move(dst, &mag);
    return NEX_CONVERT_OK_E;
}

nex_convert_err_ty nex_convert_decimal_to_dec(bigint_dec_ty *dst,
        const bigdecimal_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigdecimal_is_nan(src) || bigdecimal_is_inf(src)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (bigdecimal_is_zero(src)) {
        const bigint_err_ty zer = bigint_dec_from_u64(dst, 0U);
        return map_bi_err(zer);
    }
    if (src->exp < 0) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    bigint_dec_ty work;
    bigint_err_ty berr = bigint_dec_init(&work);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_dec_copy(&work, &src->mant);
    if (berr == BIGINT_OK_E && src->exp > 0) {
        berr = bigint_dec_mul_pow10(&work, (size_t)src->exp);
    }
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&work);
        return map_bi_err(berr);
    }
    if (src->flag == BIGDECIMAL_NEG_E) {
        bigint_dec_neg(&work);
    }
    bigint_dec_move(dst, &work);
    return NEX_CONVERT_OK_E;
}

/* ------------------------------------------------------------------ */
/* 有损互转（§10）                                                     */
/* ------------------------------------------------------------------ */

/*
 * brief: 二进制保护位除法 + 一次舍入（frac → bigfloat 核心）
 * note: 镜像 bigfloat_div 的移位公式：shift = mant_bits + 1 − bl1 + bl2，
 *       商位长 ∈ {mant_bits+1, mant_bits+2}；余数非零置粘位
 */
nex_convert_err_ty nex_convert_frac_to_float(bigfloat_ty *dst,
        const bigfrac_ty *src, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    const int sign = (bigint_bin_sign(&src->num) == BIGINT_SIGN_NEG_E) ? -1 : 1;
    const size_t bl1 = bigint_bin_bit_len(&src->num);
    const size_t bl2 = bigint_bin_bit_len(&src->den);
    const int64_t shift = (int64_t)ctx->mant_bits + 1 - (int64_t)bl1
            + (int64_t)bl2;

    bigint_bin_ty num;
    bigint_bin_ty den;
    bigint_bin_ty quot;
    bigint_bin_ty rem;
    bigint_err_ty berr;
    bigint_err_ty btmp = bigint_bin_init(&num);
    if (btmp != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&den);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&num);
        return NEX_CONVERT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&quot);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        return NEX_CONVERT_ERR_OOM_E;
    }
    btmp = bigint_bin_init(&rem);
    if (btmp != BIGINT_OK_E) {
        bigint_bin_free(&num);
        bigint_bin_free(&den);
        bigint_bin_free(&quot);
        return NEX_CONVERT_ERR_OOM_E;
    }
    if (shift >= 0) {
        berr = bigint_bin_copy(&num, &src->num);
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_abs(&num);  // 幅值：符号经 sign 传递
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_shl(&num, &num, (size_t)shift);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_copy(&den, &src->den);
        }
    } else {
        berr = bigint_bin_copy(&num, &src->num);
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_abs(&num);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_copy(&den, &src->den);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_shl(&den, &den, (size_t)(-shift));
        }
    }
    if (berr != BIGINT_OK_E) {
        goto cleanup;
    }
    berr = bigint_bin_div_rem(&quot, &rem, &num, &den);
    if (berr != BIGINT_OK_E) {
        goto cleanup;
    }
    {
        const bool sticky = !bigint_bin_is_zero(&rem);
        const bigfloat_err_ty ferr = nex_bf_round_pack(dst, &quot, -shift,
                sticky, sign, ctx);
        berr = (ferr == BIGFLOAT_OK_E) ? BIGINT_OK_E
                : (ferr == BIGFLOAT_ERR_OOM_E) ? BIGINT_ERR_OOM_E
                : BIGINT_ERR_INVALID_E;
    }
cleanup:
    bigint_bin_free(&num);
    bigint_bin_free(&den);
    bigint_bin_free(&quot);
    bigint_bin_free(&rem);
    return map_bi_err(berr);
}

/*
 * brief: 十进制保护位除法 + 一次舍入（frac → bigdecimal 核心）
 */
nex_convert_err_ty nex_convert_frac_to_decimal(bigdecimal_ty *dst,
        const bigfrac_ty *src, const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    const int sign = (bigint_bin_sign(&src->num) == BIGINT_SIGN_NEG_E) ? -1 : 1;

    bigint_dec_ty num;
    bigint_dec_ty den;
    bigint_dec_ty quot;
    bigint_dec_ty rem;
    bigint_err_ty berr;
    bigint_err_ty btmp = bigint_dec_init(&num);
    if (btmp != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&den);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&num);
        return NEX_CONVERT_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&quot);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&num);
        bigint_dec_free(&den);
        return NEX_CONVERT_ERR_OOM_E;
    }
    btmp = bigint_dec_init(&rem);
    if (btmp != BIGINT_OK_E) {
        bigint_dec_free(&num);
        bigint_dec_free(&den);
        bigint_dec_free(&quot);
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_conv_bin_to_dec(&num, &src->num);
    if (berr != BIGINT_OK_E) {
        goto cleanup;
    }
    berr = bigint_conv_bin_to_dec(&den, &src->den);
    if (berr != BIGINT_OK_E) {
        goto cleanup;
    }
    bigint_dec_abs(&num);  // 幅值：符号经 sign 传递
    {
        const size_t dl1 = bigint_dec_digit_len(&num);
        const size_t dl2 = bigint_dec_digit_len(&den);
        const int64_t shift = (int64_t)ctx->mant_digits + 1 - (int64_t)dl1
                + (int64_t)dl2;
        if (shift >= 0) {
            berr = bigint_dec_mul_pow10(&num, (size_t)shift);
        } else {
            berr = bigint_dec_mul_pow10(&den, (size_t)(-shift));
        }
        if (berr != BIGINT_OK_E) {
            goto cleanup;
        }
        berr = bigint_dec_div_rem(&quot, &rem, &num, &den);
        if (berr != BIGINT_OK_E) {
            goto cleanup;
        }
        const bool sticky = !bigint_dec_is_zero(&rem);
        const bigdecimal_err_ty ferr = nex_dec_round_pack(dst, &quot, -shift,
                sticky, sign, ctx);
        berr = (ferr == BIGDECIMAL_OK_E) ? BIGINT_OK_E
                : (ferr == BIGDECIMAL_ERR_OOM_E) ? BIGINT_ERR_OOM_E
                : BIGINT_ERR_INVALID_E;
    }
cleanup:
    bigint_dec_free(&num);
    bigint_dec_free(&den);
    bigint_dec_free(&quot);
    bigint_dec_free(&rem);
    return map_bi_err(berr);
}

nex_convert_err_ty nex_convert_dec_to_float(bigfloat_ty *dst,
        const bigint_dec_ty *src, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    bigint_bin_ty bin;
    bigint_err_ty berr = bigint_bin_init(&bin);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_conv_dec_to_bin(&bin, src);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&bin);
        return map_bi_err(berr);
    }
    bigint_bin_abs(&bin);  // 幅值：符号经 round_pack 的 sign 传递
    const bigfloat_err_ty ferr = nex_bf_round_pack(dst, &bin, 0, false,
            (bigint_dec_sign(src) == BIGINT_SIGN_NEG_E) ? -1 : 1, ctx);
    bigint_bin_free(&bin);
    return (ferr == BIGFLOAT_OK_E) ? NEX_CONVERT_OK_E
            : (ferr == BIGFLOAT_ERR_OOM_E ? NEX_CONVERT_ERR_OOM_E
                    : NEX_CONVERT_ERR_INVALID_E);
}

/*
 * brief: bigfloat → bigdecimal（经精确有理数，一次舍入）
 * note: value = mant × 2^exp；exp < 0 时 value = mant × 5^|exp| / 10^|exp|，
 *       分子转十进制后 round_pack 一次舍入；exp ≥ 0 时为整数，直接转
 *       十进制后 round_pack
 */
nex_convert_err_ty nex_convert_float_to_decimal(bigdecimal_ty *dst,
        const bigfloat_ty *src, const bigdecimal_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    const int sign = ((src->flag == BIGFLOAT_NEG_E)
            || (src->flag == BIGFLOAT_NEG_ZERO_E)
            || (src->flag == BIGFLOAT_NEG_INF_E)) ? -1 : 1;
    if (bigfloat_is_nan(src) || bigfloat_is_inf(src)) {
        /* 特殊值按目标语义映射：±∞ → ±∞；NaN → NaN */
        bigdecimal_free(dst);
        bigdecimal_init(dst);
        dst->flag = bigfloat_is_nan(src) ? BIGDECIMAL_NAN_E
                : (sign < 0 ? BIGDECIMAL_NEG_INF_E : BIGDECIMAL_POS_INF_E);
        return NEX_CONVERT_OK_E;
    }
    if (bigfloat_is_zero(src)) {
        bigdecimal_free(dst);
        bigdecimal_init(dst);
        dst->flag = (sign < 0) ? BIGDECIMAL_NEG_ZERO_E
                : BIGDECIMAL_POS_ZERO_E;
        return NEX_CONVERT_OK_E;
    }

    /* 分子（十进制）*/
    bigint_dec_ty num_dec;
    bigint_err_ty berr = bigint_dec_init(&num_dec);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_conv_bin_to_dec(&num_dec, &src->mant);
    if (berr != BIGINT_OK_E) {
        bigint_dec_free(&num_dec);
        return map_bi_err(berr);
    }
    int64_t exp10 = 0;
    if (src->exp >= 0) {
        /* 整数：value = (mant << exp) 十进制表示，指数 0 */
        bigint_bin_ty bin;
        berr = bigint_bin_init(&bin);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&num_dec);
            return NEX_CONVERT_ERR_OOM_E;
        }
        berr = bigint_bin_copy(&bin, &src->mant);
        if (berr == BIGINT_OK_E && src->exp > 0) {
            berr = bigint_bin_shl(&bin, &bin, (size_t)src->exp);
        }
        if (berr == BIGINT_OK_E) {
            bigint_dec_free(&num_dec);
            berr = bigint_conv_bin_to_dec(&num_dec, &bin);
        }
        bigint_bin_free(&bin);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&num_dec);
            return map_bi_err(berr);
        }
    } else {
        /* value = mant × 5^|exp| / 10^|exp|：分子 = dec(mant) × 5^|exp| */
        bigint_dec_ty five;
        bigint_dec_ty pw;
        berr = bigint_dec_init(&five);
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_init(&pw);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_from_u64(&five, 5U);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_pow(&pw, &five, (uint64_t)(-(src->exp)));
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_dec_mul(&num_dec, &num_dec, &pw);
        }
        bigint_dec_free(&five);
        bigint_dec_free(&pw);
        if (berr != BIGINT_OK_E) {
            bigint_dec_free(&num_dec);
            return map_bi_err(berr);
        }
        exp10 = src->exp;  // 负指数：value = 分子 × 10^exp10
    }
    const bigdecimal_err_ty ferr = nex_dec_round_pack(dst, &num_dec, exp10,
            false, sign, ctx);
    bigint_dec_free(&num_dec);
    return (ferr == BIGDECIMAL_OK_E) ? NEX_CONVERT_OK_E
            : (ferr == BIGDECIMAL_ERR_OOM_E ? NEX_CONVERT_ERR_OOM_E
                    : NEX_CONVERT_ERR_INVALID_E);
}

/*
 * brief: bigdecimal → bigfloat（经精确有理数，一次舍入）
 * note: value = mant_dec × 10^exp10；exp10 ≥ 0 时为整数，转二进制后
 *       round_pack；exp10 < 0 时 value = num_bin / 10^|exp10|，二进制
 *       保护位除法 + round_pack
 */
nex_convert_err_ty nex_convert_decimal_to_float(bigfloat_ty *dst,
        const bigdecimal_ty *src, const bigfloat_ctx_ty *ctx)
{
    if ((dst == NULL) || (src == NULL) || (ctx == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    const int sign = ((src->flag == BIGDECIMAL_NEG_E)
            || (src->flag == BIGDECIMAL_NEG_ZERO_E)
            || (src->flag == BIGDECIMAL_NEG_INF_E)) ? -1 : 1;
    if (bigdecimal_is_nan(src) || bigdecimal_is_inf(src)) {
        bigfloat_free(dst);
        bigfloat_init(dst);
        dst->flag = bigdecimal_is_nan(src) ? BIGFLOAT_NAN_E
                : (sign < 0 ? BIGFLOAT_NEG_INF_E : BIGFLOAT_POS_INF_E);
        return NEX_CONVERT_OK_E;
    }
    if (bigdecimal_is_zero(src)) {
        bigfloat_free(dst);
        bigfloat_init(dst);
        dst->flag = (sign < 0) ? BIGFLOAT_NEG_ZERO_E : BIGFLOAT_POS_ZERO_E;
        return NEX_CONVERT_OK_E;
    }

    /* 分子（二进制）*/
    bigint_bin_ty num;
    bigint_err_ty berr = bigint_bin_init(&num);
    if (berr != BIGINT_OK_E) {
        return NEX_CONVERT_ERR_OOM_E;
    }
    berr = bigint_conv_dec_to_bin(&num, &src->mant);
    if (berr != BIGINT_OK_E) {
        bigint_bin_free(&num);
        return map_bi_err(berr);
    }
    bigfloat_err_ty ferr;
    if (src->exp >= 0) {
        /* 整数：num × 10^exp10 直接 round_pack（指数 0） */
        if (src->exp > 0) {
            bigint_bin_ty ten;
            bigint_bin_ty pw;
            berr = bigint_bin_init(&ten);
            if (berr == BIGINT_OK_E) {
                berr = bigint_bin_init(&pw);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_bin_from_u64(&ten, 10U);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_bin_pow(&pw, &ten, (uint64_t)src->exp);
            }
            if (berr == BIGINT_OK_E) {
                berr = bigint_bin_mul(&num, &num, &pw);
            }
            bigint_bin_free(&ten);
            bigint_bin_free(&pw);
        }
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&num);
            return map_bi_err(berr);
        }
        ferr = nex_bf_round_pack(dst, &num, 0, false, sign, ctx);
    } else {
        /* value = num / 10^k（k = |exp10|）：分母直接用 10^k 的二进制形式，
         * 保护位除法 + 一次舍入（含 2^k × 5^k 的全部因子） */
        const uint64_t k = (uint64_t)(-(src->exp));
        bigint_bin_ty den;
        bigint_bin_ty ten;
        berr = bigint_bin_init(&den);
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_init(&ten);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_from_u64(&ten, 10U);
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_pow(&den, &ten, k);
        }
        bigint_bin_free(&ten);
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&num);
            bigint_bin_free(&den);
            return map_bi_err(berr);
        }
        /* 镜像 frac → float 的保护位除法 */
        const size_t bl1 = bigint_bin_bit_len(&num);
        const size_t bl2 = bigint_bin_bit_len(&den);
        const int64_t shift = (int64_t)ctx->mant_bits + 1 - (int64_t)bl1
                + (int64_t)bl2;
        bigint_bin_ty quot;
        bigint_bin_ty rem;
        bigint_err_ty btmp = bigint_bin_init(&quot);
        if (btmp != BIGINT_OK_E) {
            bigint_bin_free(&num);
            bigint_bin_free(&den);
            return NEX_CONVERT_ERR_OOM_E;
        }
        btmp = bigint_bin_init(&rem);
        if (btmp != BIGINT_OK_E) {
            bigint_bin_free(&num);
            bigint_bin_free(&den);
            bigint_bin_free(&quot);
            return NEX_CONVERT_ERR_OOM_E;
        }
        if (shift >= 0) {
            berr = bigint_bin_shl(&num, &num, (size_t)shift);
        } else {
            berr = bigint_bin_shl(&den, &den, (size_t)(-shift));
        }
        if (berr == BIGINT_OK_E) {
            berr = bigint_bin_div_rem(&quot, &rem, &num, &den);
        }
        if (berr != BIGINT_OK_E) {
            bigint_bin_free(&num);
            bigint_bin_free(&den);
            bigint_bin_free(&quot);
            bigint_bin_free(&rem);
            return map_bi_err(berr);
        }
        {
            const bool sticky = !bigint_bin_is_zero(&rem);
            ferr = nex_bf_round_pack(dst, &quot, -shift, sticky, sign, ctx);
        }
        bigint_bin_free(&quot);
        bigint_bin_free(&rem);
        bigint_bin_free(&den);
    }
    bigint_bin_free(&num);
    return (ferr == BIGFLOAT_OK_E) ? NEX_CONVERT_OK_E
            : (ferr == BIGFLOAT_ERR_OOM_E ? NEX_CONVERT_ERR_OOM_E
                    : NEX_CONVERT_ERR_INVALID_E);
}

/* ------------------------------------------------------------------ */
/* 复数 → 实数（§10）                                                  */
/* ------------------------------------------------------------------ */

nex_convert_err_ty nex_convert_cpx_float_to_float(bigfloat_ty *dst,
        const bigcomplex_float_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (!bigfloat_is_zero(&src->im)) {
        return NEX_CONVERT_ERR_INVALID_E;  // 仅当虚部为 ±0
    }
    return (bigfloat_copy(dst, &src->re) == BIGFLOAT_OK_E)
            ? NEX_CONVERT_OK_E : NEX_CONVERT_ERR_OOM_E;
}

nex_convert_err_ty nex_convert_cpx_decimal_to_decimal(bigdecimal_ty *dst,
        const bigcomplex_decimal_ty *src)
{
    if ((dst == NULL) || (src == NULL)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    if (!bigdecimal_is_zero(&src->im)) {
        return NEX_CONVERT_ERR_INVALID_E;
    }
    return (bigdecimal_copy(dst, &src->re) == BIGDECIMAL_OK_E)
            ? NEX_CONVERT_OK_E : NEX_CONVERT_ERR_OOM_E;
}
