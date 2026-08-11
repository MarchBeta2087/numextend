#ifndef NEX_BIGFLOAT_INTERNAL_H
#define NEX_BIGFLOAT_INTERNAL_H

#include "nex/bigfloat/nex_bigfloat.h"

/*
 * bigfloat 模块内部共享接口（nex 库内部使用，不对外公开）。
 * 供 nex_bigfloat.c（核心算术）与 nex_bigfloat_str.c（十进制字符串 I/O）
 * 两个实现单元共用。
 */

/*
 * 将 value = raw_mant × 2^raw_exp（raw_mant 非负）+ sticky_extra 按 ctx
 * 舍入与规范化，结果写入 dst：
 *   - 舍入到 ctx->mant_bits 位（含保护位语义，正确舍入）；
 *   - 归一化尾数最高位恒 1（恰好 mant_bits 位，除非值为零）；
 *   - 指数范围检查：上溢 → 按舍入方向 ±∞ 或最大有限值；下溢 → flush-to-zero
 *     （±0，符号按舍入方向）。
 * param: sign 结果符号（±1），仅用于正常值/溢出/下溢的符号传播；
 *        raw_mant 为零时的 +0/−0 符号按舍入规则（向 −∞ 舍入的负结果 → −0）。
 * return: 成功返回 BIGFLOAT_OK_E；内存不足返回 BIGFLOAT_ERR_OOM_E
 */
bigfloat_err_ty nex_bf_round_pack(bigfloat_ty *dst, const bigint_bin_ty *raw_mant,
        int64_t raw_exp, bool sticky_extra, int sign, const bigfloat_ctx_ty *ctx);

/*
 * 将十进制值 value = decimal_mant × 10^exp10（decimal_mant 非负，可为任意大）
 * 按 ctx 正确舍入为 bigfloat（from_str 的内部共享实现）。
 * param: negative 十进制值的符号
 * return: 成功返回 BIGFLOAT_OK_E；OOM 返回 BIGFLOAT_ERR_OOM_E
 */
bigfloat_err_ty nex_bf_from_decimal(bigfloat_ty *dst, const bigint_bin_ty *decimal_mant,
        int64_t exp10, bool negative, const bigfloat_ctx_ty *ctx);

/*
 * 将 src 最近舍入到 n 位十进制有效数字（"最短往返"候选生成，to_str 内部使用）。
 * param: digits 输出缓冲区，接收恰好 n 位数字（不含小数点/符号/指数），无前导零
 * param: digits_len digits 缓冲区长度（必须 ≥ n + 1，含 '\0'）
 * param: needed 若非 NULL，传出含 '\0' 的所需长度
 * param: dec_exp 非 NULL 时传出十进制科学指数（小数点位于第 1 位数字后）
 * return: 成功返回 BIGFLOAT_OK_E；digits 不足返回 BIGFLOAT_ERR_OVERFLOW_E
 *         （needed 写出）；src 非有限值返回 BIGFLOAT_ERR_INVALID_E
 */
bigfloat_err_ty nex_bf_dec_round(const bigfloat_ty *src, size_t n,
        char *digits, size_t digits_len, size_t *needed, int64_t *dec_exp);

#endif /* NEX_BIGFLOAT_INTERNAL_H */