#ifndef NEX_BIGINT_BLOCK_H
#define NEX_BIGINT_BLOCK_H

#include "nex/bigint/bin/nex_bigint_bin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nex_bigint_block：contrib 层分块/二分 NTT 乘法（P1，issue #98）。
 *
 * 把输入按肢数折半：
 *   a = a0 + a1·B^L, b = b0 + b1·B^L
 *   a·b = c0 + (sa·sb − c0 − c2)·B^L + c2·B^(2L)
 * 其中 c0=a0·b0、c2=a1·b1、sa=a0+a1、sb=b0+b1；三个子积均走核心
 * 多模数 CRT NTT。用于突破单次 NTT 的 2^27 肢和上限：半积 s≈len/2，
 * 使 10^9 位（约 1.04e8 肢）可在 2^27 上限内完成。
 *
 * 限制：半积 s = 2·ceil(max_len/2) 必须 ≤ NEX_MUL_NTT_MAX_SUM（2^27）；
 * 更大规模（10^10 位）需要多级分块/SSA，不在本实验范围。
 */
bigint_err_ty nex_block_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

#ifdef __cplusplus
}
#endif

#endif /* NEX_BIGINT_BLOCK_H */
