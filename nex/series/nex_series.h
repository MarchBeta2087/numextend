#ifndef NEX_SERIES_H
#define NEX_SERIES_H

#include <stddef.h>

#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigfrac/nex_bigfrac.h"

/*
 * nex_series：幂级数求值的「严格尾界 + 扩展精度求值」路线 A（§15.6）。
 *
 * 在数值点求值时，部分和与阶估计均以 `p + 48 + log2 n` 工作精度递推（尾界
 * 用向上舍入，保证上界），最终一次舍入到目标 `ctx`；不再构造精确有理公共
 * 分母，高精度（≳256 bit）下快 1–2 个数量级。截断尾仍由尾界界定：
 *   - 几何型 `A·q^n/(1−q)`（q = B·r）：有界系数（如二项式 `α ≥ −1`）；
 *   - 比值型 `t_n/(1−q_n)`：阶乘型系数（exp / sin / cos）。
 * 求值返回结果，并可经可选 `err` 输出收到**该截断的严格上界**。
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
 *         alpha < −1（系数无界）返回 BIGFRAC_ERR_UNSUPPORTED_E；
 *         内存不足返回 BIGFRAC_ERR_OOM_E（A、B 不变）
 */
bigfrac_err_ty nex_series_binom_model(bigfrac_ty *A, bigfrac_ty *B,
        const bigfrac_ty *alpha);

/*
 * brief: 二项式级数自适应阶：最小 n 使 (1+z)^alpha（|z| ≤ r）截断尾 ≤ eps
 * return: 成功返回 BIGFRAC_OK_E；参数非法或 r ≥ 1 返回 BIGFRAC_ERR_INVALID_E；
 *         alpha < −1 返回 BIGFRAC_ERR_UNSUPPORTED_E；内存不足返回
 *         BIGFRAC_ERR_OOM_E（out_n 不变）
 */
bigfrac_err_ty nex_series_binom_order(size_t *out_n, const bigfrac_ty *alpha,
        const bigfrac_ty *r, const bigfrac_ty *eps);

/*
 * brief: 在 bigfloat 点求值 (1+z)^alpha（精确系数 + 全局尾界 + 终值舍入）
 * param: out     输出（正确舍入的精确部分和）
 * param: err_out 可选，接收截断尾的严格上界；可为 NULL
 * param: alpha   指数（≥ −1）
 * param: z       自变量（有限且 |z| < 1）
 * param: eps     目标截断误差（绝对，≥ 0）
 * param: ctx     目标精度上下文
 * return: 成功返回 BIGFLOAT_OK_E；参数非法、z 非有限、|z| ≥ 1 或 alpha < −1
 *         返回 BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 * note: 总误差 ≤ eps（截断）+ 终值一次舍入
 */
bigfloat_err_ty nex_series_binom_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfrac_ty *alpha, const bigfloat_ty *z,
        const bigfrac_ty *eps, const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 (1+z)^alpha（语义同 eval_float）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 eval_float（错误码对应更换）
 */
bigdecimal_err_ty nex_series_binom_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigfrac_ty *alpha,
        const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: exp(z) = Σ z^k/k! 的自适应阶：最小 n 使截断尾（|z| ≤ r）≤ eps
 * return: 成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E；
 *         超出迭代上限返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E
 */
bigfrac_err_ty nex_series_exp_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps);

/*
 * brief: cos(z) = Σ (−1)^j z^{2j}/(2j)! 的自适应阶
 * return: 同 exp_order
 */
bigfrac_err_ty nex_series_cos_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps);

/*
 * brief: sin(z) = Σ (−1)^j z^{2j+1}/(2j+1)! 的自适应阶
 * return: 同 exp_order
 */
bigfrac_err_ty nex_series_sin_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps);

/*
 * brief: 在 bigfloat 点求值 exp(z)（精确系数 + 比值尾界）
 * param: out/err_out/z/eps/ctx 语义同 binom_eval_float
 * return: 成功返回 BIGFLOAT_OK_E；参数非法或 z 非有限返回
 *         BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（out 不变）
 * note: 未做实参规约，建议 |z| ≲ 1；总误差 ≤ eps + 终值一次舍入
 *       （工作精度保护位远高于目标 ulp）
 */
bigfloat_err_ty nex_series_exp_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigfloat 点求值 cos(z)（语义同 exp_eval_float）
 * return: 同 exp_eval_float
 */
bigfloat_err_ty nex_series_cos_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigfloat 点求值 sin(z)（语义同 exp_eval_float）
 * return: 同 exp_eval_float
 */
bigfloat_err_ty nex_series_sin_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 exp(z)（语义同 bigfloat 版）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）
 */
bigdecimal_err_ty nex_series_exp_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 cos(z)（语义同 bigfloat 版）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）
 */
bigdecimal_err_ty nex_series_cos_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 sin(z)（语义同 bigfloat 版）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）
 */
bigdecimal_err_ty nex_series_sin_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z, const bigfrac_ty *eps,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: ln(1+z) = Σ_{k≥1} (−1)^{k+1} z^k/k 的自适应阶（|z| ≤ r < 1）
 * param: out_n 输出阶；r 自变量幅度上界（需 r < 1）；eps 目标误差
 * return: 成功返回 BIGFRAC_OK_E；参数非法或 r ≥ 1 返回 BIGFRAC_ERR_INVALID_E；
 *         超出迭代上限返回 BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E
 */
bigfrac_err_ty nex_series_ln1p_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps);

/*
 * brief: 在 bigfloat 点求值 ln(1+z)（精确系数 + 比值尾界 + 可选误差输出）
 * param: out/err_out/z/eps/ctx 语义同 binom_eval_float
 * return: 成功返回 BIGFLOAT_OK_E；参数非法、z 非有限或 |z| ≥ 1 返回
 *         BIGFLOAT_ERR_INVALID_E；内存不足返回 BIGFLOAT_ERR_OOM_E（out 不变）
 * note: 未做实参规约，建议 |z| ≲ 1/2；总误差 ≤ eps + 终值一次舍入
 *       （工作精度保护位远高于目标 ulp）
 */
bigfloat_err_ty nex_series_ln1p_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 ln(1+z)（语义同 bigfloat 版）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）
 */
bigdecimal_err_ty nex_series_ln1p_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);

/*
 * brief: atan(z) = Σ_{k≥0} (−1)^k z^{2k+1}/(2k+1) 的自适应阶（|z| ≤ r < 1）
 * return: 同 ln1p_order
 */
bigfrac_err_ty nex_series_atan_order(size_t *out_n, const bigfrac_ty *r,
        const bigfrac_ty *eps);

/*
 * brief: 在 bigfloat 点求值 atan(z)（语义同 ln1p_eval_float）
 * return: 成功返回 BIGFLOAT_OK_E；其余同 ln1p_eval_float
 */
bigfloat_err_ty nex_series_atan_eval_float(bigfloat_ty *out,
        bigfloat_ty *err_out, const bigfloat_ty *z, const bigfrac_ty *eps,
        const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 atan(z)（语义同 ln1p_eval_decimal）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 ln1p_eval_float（错误码对应更换）
 */
bigdecimal_err_ty nex_series_atan_eval_decimal(bigdecimal_ty *out,
        bigdecimal_ty *err_out, const bigdecimal_ty *z,
        const bigfrac_ty *eps, const bigdecimal_ctx_ty *ctx);

/*
 * brief: Padé 逼近 [m/n]：由幂级数 c_0+c_1 z+… 求 P/Q，f − P/Q = O(z^{m+n+1})
 * param: num    输出分子多项式（bigfrac_series，val=0，次数 ≤ m）
 * param: den    输出分母多项式（bigfrac_series，val=0，Q(0)=1，次数 ≤ n）
 * param: series 输入幂级数（要求 val = 0）
 * param: m, n   分子/分母次数
 * return: 成功返回 BIGFRAC_OK_E；参数非法或 series->val ≠ 0 或系统奇异返回
 *         BIGFRAC_ERR_INVALID_E；内存不足返回 BIGFRAC_ERR_OOM_E
 * note: 对 q 用 bigfrac 精确高斯消元；P/Q 系数精确
 */
bigfrac_err_ty nex_series_pade_frac(bigfrac_series_ty *num,
        bigfrac_series_ty *den, const bigfrac_series_ty *series, size_t m,
        size_t n);

/*
 * brief: Padé 有理逼近求值 out = P(z)/Q(z)（精确有理结果）
 * return: 成功返回 BIGFRAC_OK_E；参数非法返回 BIGFRAC_ERR_INVALID_E；
 *         Q(z) == 0 返回 BIGFRAC_ERR_DIV_ZERO_E；内存不足返回 OOM（out 不变）
 */
bigfrac_err_ty nex_series_pade_eval_frac(bigfrac_ty *out,
        const bigfrac_series_ty *num, const bigfrac_series_ty *den,
        const bigfrac_ty *z);

/*
 * brief: 在 bigfloat 点求值 P(z)/Q(z)（精确部分和 + 终值一次舍入）
 * return: 成功返回 BIGFLOAT_OK_E；参数非法或 Q(z) == 0 返回
 *         BIGFLOAT_ERR_INVALID_E；内存不足返回 OOM（out 不变）
 */
bigfloat_err_ty nex_series_pade_eval_float(bigfloat_ty *out,
        const bigfrac_series_ty *num, const bigfrac_series_ty *den,
        const bigfloat_ty *z, const bigfloat_ctx_ty *ctx);

/*
 * brief: 在 bigdecimal 点求值 P(z)/Q(z)（语义同 bigfloat 版）
 * return: 成功返回 BIGDECIMAL_OK_E；其余同 bigfloat 版（错误码对应更换）
 */
bigdecimal_err_ty nex_series_pade_eval_decimal(bigdecimal_ty *out,
        const bigfrac_series_ty *num, const bigfrac_series_ty *den,
        const bigdecimal_ty *z, const bigdecimal_ctx_ty *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NEX_SERIES_H */
