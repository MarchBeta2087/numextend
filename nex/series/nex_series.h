#ifndef NEX_SERIES_H
#define NEX_SERIES_H

#include <stddef.h>

#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigfrac/nex_bigfrac.h"

/*
 * nex_series：幂级数求值的「精确系数 + 全局尾界」路线 A（设计文档 §15.6）。
 *
 * 以精确有理解的截断级数在数值点求值：系数为 `bigfrac`（精确），仅截断与
 * 最终舍入引入误差；截断尾由几何型全局尾界 `A·q^n/(1−q)`（q = B·r）界定。
 * 球式（中心 + 半径）路线 B 为后续方向。
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * brief: 二项式级数 (1+z)^alpha 的全局尾界模型 (A, B)：|C(alpha,k)| ≤ A·B^k
 * param: A     系数幅度上界（输出）
 * param: B     系数增长因子（输出，此处恒为 1）
 * param: alpha 指数
 * return: 成功返回 BIGFRAC_OK_E；指针为 NULL 返回 BIGFRAC_ERR_INVALID_E；
 *         alpha < −1（系数无界，需用比值型界）返回 BIGFRAC_ERR_UNSUPPORTED_E；
 *         内存不足返回 BIGFRAC_ERR_OOM_E（A、B 不变）
 * note: alpha ≥ −1 时 |C(alpha,k)| 在 k ≈ alpha 处取峰、其后单调下降，
 *       故扫描前若干项即可得精确的 A（B = 1）
 */
bigfrac_err_ty nex_series_binom_model(bigfrac_ty *A, bigfrac_ty *B,
        const bigfrac_ty *alpha);

/*
 * brief: 自适应截断阶：最小 n 使 (1+z)^alpha（|z| ≤ r）的截断尾 ≤ eps
 * param: out_n 输出阶 n
 * param: alpha 指数
 * param: r     自变量幅度上界（≥ 0，需 r < 1）
 * param: eps   目标绝对误差上界（≥ 0）
 * return: 成功返回 BIGFRAC_OK_E；参数非法或 r ≥ 1 返回 BIGFRAC_ERR_INVALID_E；
 *         alpha < −1 返回 BIGFRAC_ERR_UNSUPPORTED_E；Q 极接近 1 超上限返回
 *         BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E（out_n 不变）
 */
bigfrac_err_ty nex_series_binom_order(size_t *out_n, const bigfrac_ty *alpha,
        const bigfrac_ty *r, const bigfrac_ty *eps);

/*
 * brief: 在 bigfloat 点求值 (1+z)^alpha（路线 A：精确系数 + 全局尾界 + 终值舍入）
 * param: out   输出
 * param: alpha 指数（≥ −1）
 * param: z     自变量（有限且 |z| < 1）
 * param: eps   目标截断误差（绝对，≥ 0）
 * param: ctx   目标精度上下文
 * return: 成功返回 BIGFLOAT_OK_E；参数非法、z 非有限或 |z| ≥ 1、或 alpha < −1
 *         返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 *         （out 不变）
 * note: 误差 ≤ eps（截断）+ 终值一次舍入误差；返回的 out 是精确部分和的
 *       正确舍入
 */
bigfloat_err_ty nex_series_binom_eval_float(bigfloat_ty *out,
        const bigfrac_ty *alpha, const bigfloat_ty *z,
        const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 (1+z)^alpha（路线 A，语义同 eval_float）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 eval_float（错误码对应更换） */
bigdecimal_err_ty nex_series_binom_eval_decimal(bigdecimal_ty *out,
        const bigfrac_ty *alpha, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NEX_SERIES_H */
