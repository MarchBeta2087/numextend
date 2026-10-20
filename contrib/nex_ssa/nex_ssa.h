#ifndef NEX_SSA_H
#define NEX_SSA_H

#include "nex/bigint/bin/nex_bigint_bin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nex_ssa：Schönhage–Strassen 原型（P1b-SSA，issue #102）。
 *
 * 参数：N bit → n=m=2^(k−1) 块、L=2n、环 Z/(2^L±1)；用两个环的循环
 * 卷积 + CRT 恢复精确系数，再按 base 2^m 进位。
 * 仅 contrib 实验；失败/规模过小时返回 UNSUPPORTED 或回退直接乘法。
 */

/*
 * brief: SSA 原型乘法：dst = lhs × rhs（仅 contrib 实验）
 * param: dst 目标（已初始化；允许与 lhs/rhs 别名）
 * param: lhs/rhs 非负操作数
 * return: BIGINT_OK_E；规模过小或环参数不支持时返回 BIGINT_ERR_UNSUPPORTED_E
 * note: 结果为精确值；1e6–1e8 位原型阶段，M2/M3 已冻结（TODO §K2）
 */
bigint_err_ty nex_ssa_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

#ifdef __cplusplus
}
#endif

#endif /* NEX_SSA_H */
