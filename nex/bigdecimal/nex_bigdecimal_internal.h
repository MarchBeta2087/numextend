#ifndef NEX_BIGDECIMAL_INTERNAL_H
#define NEX_BIGDECIMAL_INTERNAL_H

#include "nex/bigdecimal/nex_bigdecimal.h"

/*
 * bigdecimal 模块内部共享接口（nex 库内部使用，不对外公开）。
 * 供 nex_bigdecimal.c（核心算术）与 nex_bigdecimal_str.c（十进制字符串 I/O）
 * 两个实现单元共用。
 */

/*
 * 将 value = ±raw_mant × 10^raw_exp（raw_mant 非负十进制整数）+ 粘位按
 * ctx 舍入与规范化，结果写入 dst：
 *   - 精确值时 raw_mant 位数 ≤ mant_digits，直接规范化（去尾随零）；
 *   - 位数超出时按舍入位（第 mant_digits+1 位数字）与低位粘性正确舍入；
 *   - 除法/开方调用方契约：不精确结果必须提供 ≥ mant_digits + 1 位商，
 *     粘位（余数非零等）经 sticky 传入——sticky 仅在
 *     raw_mant 位数 > mant_digits 时有效（正确舍入的充分条件）；
 *   - 指数范围检查：上溢 → 按舍入方向 ±∞ 或最大有限值
 *     （10^mant_digits − 1，指数 emax）；下溢 → flush-to-zero，
 *     零符号按 IEEE 754-2008 §7.5（向 +∞ 舍入恒 +0，其余取精确结果符号）。
 * param: sign 结果符号（±1），仅用于正常值/溢出/下溢的符号传播；
 *        raw_mant 为零时的 +0/−0 符号 = sign。
 * return: 成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E
 */
bigdecimal_err_ty nex_dec_round_pack(bigdecimal_ty *dst,
        const bigint_dec_ty *raw_mant, int64_t raw_exp, bool sticky, int sign,
        const bigdecimal_ctx_ty *ctx);

/*
 * brief: 十进制整数平方根：q = floor(sqrt(x))，余数 rem = x − q²（均非负）
 * param: q   输出平方根（已初始化）
 * param: rem 输出余数（已初始化）
 * param: x   被开方数（非负）
 * return: 成功返回 BIGDECIMAL_OK_E；内存不足返回 BIGDECIMAL_ERR_OOM_E
 * note: 十进制逐位试商法：每轮消费 x 的两位、产出一位结果——rem = 100·rem
 *       + 两位；q = 10·q；试商 t = (20·q + d)·d（d 从 9 向下试），
 *       若 t ≤ rem 则 rem −= t 且 q 末位置 d。迭代 k = ceil(位数/2) 轮。
 *       复杂度 O(n²)，正确性优先（§8.3）
 */
bigint_err_ty nex_dec_isqrt(bigint_dec_ty *q, bigint_dec_ty *rem,
        const bigint_dec_ty *x);

#endif /* NEX_BIGDECIMAL_INTERNAL_H */
