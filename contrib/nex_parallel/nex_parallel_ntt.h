#ifndef NEX_PARALLEL_NTT_H
#define NEX_PARALLEL_NTT_H

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nex_parallel_bin_mul_ntt：contrib 层的并行多模数 CRT NTT 乘法实验。
 *
 * 与核心 bigint_bin_mul 的关系：
 *   - 不修改核心；复用公开的 nex_ntt 与 bigint_bin 类型/API；
 *   - 强制使用双模数 CRT NTT（不按 Toom/NTT 阈值分派），要求
 *     len(lhs)+len(rhs) ≤ 2^26（与核心 NTT 上限一致）；
 *   - 并行阶段：两个模数的 forward(a)/forward(b) 共 4 个独立 task，
 *     点乘 2 个 task，逆变换 2 个 task；CRT/进位串行；
 *   - 结果与核心 NTT 路径逐位一致（同一模数、同一变换、同一 CRT）。
 *
 * pool 为 NULL 时在调用线程顺序执行（用于对照/降级）。
 */

/*
 * brief: 并行多模数 CRT NTT 乘法：dst = lhs × rhs
 * param: dst 目标（已初始化）
 * param: lhs/rhs 非负操作数；len(lhs)+len(rhs) ≤ 2^26
 * param: pool 线程池；NULL 表示在调用线程顺序执行
 * return: BIGINT_OK_E / BIGINT_ERR_*（与核心乘法一致）
 * note: 结果与核心 NTT 路径逐位一致；不改变核心分派
 */
bigint_err_ty nex_parallel_bin_mul_ntt(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs,
        nex_thread_pool_ty *pool);

#ifdef __cplusplus
}
#endif

#endif /* NEX_PARALLEL_NTT_H */
