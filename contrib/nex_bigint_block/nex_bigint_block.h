#ifndef NEX_BIGINT_BLOCK_H
#define NEX_BIGINT_BLOCK_H

#include "nex/bigint/bin/nex_bigint_bin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nex_bigint_block：contrib 层递归分块 NTT 乘法（P1b，issue #98）。
 *
 * 递归折半直到单块 max_len ≤ 2^26（直接 NTT 的 s ≤ 2^27）：
 *   a = a0 + a1·B^L, b = b0 + b1·B^L
 *   a·b = c0 + (sa·sb − c0 − c2)·B^L + c2·B^(2L)
 * 其中 c0=a0·b0、c2=a1·b1、sa=a0+a1、sb=b0+b1；三个子积递归。
 * 10^9 位约一层；10^10 位约四层（81 个基础积）。
 *
 * 限制：10^10 位的分块/SSA 内存模型约 64–114 GB；34 GB 机器会在
 * 递归合并阶段 OOM（本实验实测）。更大规模需要 ≥256 GB 或 out-of-core。
 */
bigint_err_ty nex_block_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs);

#ifdef __cplusplus
}
#endif

#endif /* NEX_BIGINT_BLOCK_H */
