#ifndef NEX_BIGINT_CONV_H
#define NEX_BIGINT_CONV_H

#include "nex/bigint/nex_bigint_common.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"

/*
 * bigint_conv：bigint_bin ↔ bigint_dec 互转（设计文档 §4.2.6）。
 * 本单元是唯一同时包含 bin 与 dec 头文件的模块（§2.1 的唯一跨支线依赖）。
 * 两侧输出均保持规范化表示（含符号与零的唯一表示）。
 */

/*
 * brief: bin 转 dec（基 2^32 → 基 10^9）
 * param: dst 目标对象（已初始化）
 * param: src 源对象
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 反复除以 10^9（bin 幅值上的单肢除法，余数即十进制肢），O(n^2)
 */
bigint_err_ty bigint_conv_bin_to_dec(bigint_dec_ty *dst,
        const bigint_bin_ty *src);

/*
 * brief: dec 转 bin（基 10^9 → 基 2^32）
 * param: dst 目标对象（已初始化）
 * param: src 源对象
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（dst 不变）
 * note: 自高肢向低肢逐肢 "乘 10^9 加当前肢" 累加，O(n^2)
 */
bigint_err_ty bigint_conv_dec_to_bin(bigint_bin_ty *dst,
        const bigint_dec_ty *src);

#endif /* NEX_BIGINT_CONV_H */
