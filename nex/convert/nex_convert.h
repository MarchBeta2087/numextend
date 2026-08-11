#ifndef NEX_CONVERT_H
#define NEX_CONVERT_H

#include <stdbool.h>
#include <stddef.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"

/*
 * nex_convert：类型间转换单元（设计文档 §10 转换矩阵）。
 *
 * 本单元是库内唯一的跨类型胶水层：同时包含各模块的公开头文件，集中实现
 * §10 矩阵中的所有转换。转换分三类（§10 原则）：
 *
 *   1. 精确互转（矩阵 ✓）：不引入精度损失；bigint ↔ bigfrac、
 *      bigfloat/bigdecimal → bigfrac、bigint → bigfloat/bigdecimal（后者
 *      在目标模块的 from_bigint 中提供）；
 *   2. 有损互转（矩阵 R / O）：显式传入目标精度上下文（不提供"默认精度"
 *      的隐式有损转换）；经精确中间值（有理数）一次舍入（无双重舍入）；
 *      超范围按目标 ctx 产生 ±∞ / flush-to-zero（浮点语义，非错误）；
 *   3. 逆向精确转换（浮点 → 整数）：仅在接受精确值时成功，否则返回
 *      INVALID，不做静默截断；复数 → 实数仅当虚部为 ±0。
 *
 * 命名：nex_convert_<源>_to_<目标>，源/目标 ∈ {bin, dec, frac, float,
 * decimal, cpx_float, cpx_decimal}。
 */

/* 转换单元错误码 */
typedef enum {
    NEX_CONVERT_OK_E = 0,        // 成功
    NEX_CONVERT_ERR_OOM_E,       // 内存分配失败
    NEX_CONVERT_ERR_INVALID_E,   // 非法参数 / 精确转换要求不满足（非整数等）
    NEX_CONVERT_ERR_OVERFLOW_E   // 输出缓冲不足等
} nex_convert_err_ty;

/* ------------------------------------------------------------------ */
/* 精确互转（§10：bigint ↔ bigfrac、浮点 → bigfrac）                   */
/* ------------------------------------------------------------------ */

/*
 * brief: bigint_bin → bigfrac（den = 1，精确）
 */
nex_convert_err_ty nex_convert_bin_to_frac(bigfrac_ty *dst,
        const bigint_bin_ty *src);

/*
 * brief: bigint_dec → bigfrac（经 bin 转换单元，den = 1，精确）
 */
nex_convert_err_ty nex_convert_dec_to_frac(bigfrac_ty *dst,
        const bigint_dec_ty *src);

/*
 * brief: bigfrac → bigint_bin（仅当 den == 1；否则 INVALID，不截断）
 */
nex_convert_err_ty nex_convert_frac_to_bin(bigint_bin_ty *dst,
        const bigfrac_ty *src);

/*
 * brief: bigfrac → bigint_dec（仅当 den == 1；经 bin，否则 INVALID）
 */
nex_convert_err_ty nex_convert_frac_to_dec(bigint_dec_ty *dst,
        const bigfrac_ty *src);

/*
 * brief: bigfloat → bigfrac（精确：value = mant × 2^exp 化简为分数）
 * note: ±0 → 0/1；±∞ / NaN → INVALID
 */
nex_convert_err_ty nex_convert_float_to_frac(bigfrac_ty *dst,
        const bigfloat_ty *src);

/*
 * brief: bigdecimal → bigfrac（精确：value = mant × 10^exp 化简为分数）
 * note: ±0 → 0/1；±∞ / NaN → INVALID
 */
nex_convert_err_ty nex_convert_decimal_to_frac(bigfrac_ty *dst,
        const bigdecimal_ty *src);

/* ------------------------------------------------------------------ */
/* 逆向精确转换（§10：浮点 → 整数，仅整数值且有限）                    */
/* ------------------------------------------------------------------ */

/*
 * brief: bigfloat → bigint_bin（仅当值为有限整数；否则 INVALID）
 */
nex_convert_err_ty nex_convert_float_to_bin(bigint_bin_ty *dst,
        const bigfloat_ty *src);

/*
 * brief: bigfloat → bigint_dec（经 bin；仅当值为有限整数；否则 INVALID）
 */
nex_convert_err_ty nex_convert_float_to_dec(bigint_dec_ty *dst,
        const bigfloat_ty *src);

/*
 * brief: bigdecimal → bigint_bin（仅当值为有限整数；否则 INVALID）
 */
nex_convert_err_ty nex_convert_decimal_to_bin(bigint_bin_ty *dst,
        const bigdecimal_ty *src);

/*
 * brief: bigdecimal → bigint_dec（仅当值为有限整数；否则 INVALID）
 */
nex_convert_err_ty nex_convert_decimal_to_dec(bigint_dec_ty *dst,
        const bigdecimal_ty *src);

/* ------------------------------------------------------------------ */
/* 有损互转（§10：显式 ctx、单次舍入；超范围按目标 ctx 产生 ±∞/flush） */
/* ------------------------------------------------------------------ */

/*
 * brief: bigfrac → bigfloat（num/den 带保护位整数除法 + 一次舍入）
 */
nex_convert_err_ty nex_convert_frac_to_float(bigfloat_ty *dst,
        const bigfrac_ty *src, const bigfloat_ctx_ty *ctx);

/*
 * brief: bigfrac → bigdecimal（num/den 带保护位整数除法 + 一次舍入）
 */
nex_convert_err_ty nex_convert_frac_to_decimal(bigdecimal_ty *dst,
        const bigfrac_ty *src, const bigdecimal_ctx_ty *ctx);

/*
 * brief: bigint_dec → bigfloat（精确值可表示时无损，否则按 ctx 舍入）
 */
nex_convert_err_ty nex_convert_dec_to_float(bigfloat_ty *dst,
        const bigint_dec_ty *src, const bigfloat_ctx_ty *ctx);

/*
 * brief: bigfloat → bigdecimal（经精确有理数中间值，一次舍入，§10）
 * note: 实现走 value = mant × 2^exp → mant × 5^|exp| / 10^|exp| 的
 *       十进制精确表示后按 ctx 舍入，无双重舍入
 */
nex_convert_err_ty nex_convert_float_to_decimal(bigdecimal_ty *dst,
        const bigfloat_ty *src, const bigdecimal_ctx_ty *ctx);

/*
 * brief: bigdecimal → bigfloat（经精确有理数中间值，一次舍入，§10）
 */
nex_convert_err_ty nex_convert_decimal_to_float(bigfloat_ty *dst,
        const bigdecimal_ty *src, const bigfloat_ctx_ty *ctx);

/* ------------------------------------------------------------------ */
/* 复数 → 实数（§10：仅当虚部为 ±0，取实部）                           */
/* ------------------------------------------------------------------ */

/*
 * brief: bigcomplex_float → bigfloat（仅当 im 为 ±0；否则 INVALID）
 */
nex_convert_err_ty nex_convert_cpx_float_to_float(bigfloat_ty *dst,
        const bigcomplex_float_ty *src);

/*
 * brief: bigcomplex_decimal → bigdecimal（仅当 im 为 ±0；否则 INVALID）
 */
nex_convert_err_ty nex_convert_cpx_decimal_to_decimal(bigdecimal_ty *dst,
        const bigcomplex_decimal_ty *src);

#endif /* NEX_CONVERT_H */
