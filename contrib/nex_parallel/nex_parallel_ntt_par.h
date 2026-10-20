#ifndef NEX_PARALLEL_NTT_PAR_H
#define NEX_PARALLEL_NTT_PAR_H

#include <stddef.h>
#include <stdint.h>

#include "nex/ntt/nex_ntt.h"
#include "nex_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一个待变换的缓冲区及其模数 */
typedef struct {
    uint32_t *buf;
    const ntt_mod_ty *mod;
} nex_par_ntt_buf_ty;

/* 一个点乘三元组 */
typedef struct {
    uint32_t *dst;
    const uint32_t *lhs;
    const uint32_t *rhs;
    const ntt_mod_ty *mod;
} nex_par_ntt_pw_ty;

/*
 * 多缓冲区阶段级并行 NTT：所有缓冲区共享同一 log2n；每个 stage 只做一次
 * barrier，把 count 个缓冲区的蝶形单元一起分给线程池。结果与 nex_ntt
 * 逐位一致。pool==NULL 或单线程时逐缓冲区调用核心实现。
 */
void nex_par_ntt_forward_multi(nex_par_ntt_buf_ty *bufs, size_t count,
        uint32_t log2n, nex_thread_pool_ty *pool);

/* 多缓冲区逆向 NTT（含 N^-1 归一化），语义同 forward_multi。 */
void nex_par_ntt_inverse_multi(nex_par_ntt_buf_ty *bufs, size_t count,
        uint32_t log2n, nex_thread_pool_ty *pool);

/* 批量频域点乘：jobs[i] 相互独立；len 为每个变换长度。 */
void nex_par_ntt_pointwise_multi(nex_par_ntt_pw_ty *jobs, size_t count,
        size_t len, nex_thread_pool_ty *pool);

/* 单缓冲区便捷包装（等价于 count=1 的 multi）。 */

/* 正向 NTT：coeffs 长度 2^log2n；pool 可为 NULL（顺序执行）。 */
void nex_par_ntt_forward(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool);

/* 逆向 NTT（含 N^-1 归一化）；pool 可为 NULL（顺序执行）。 */
void nex_par_ntt_inverse(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool);

/* 频域点乘 dst = lhs·rhs mod p（逐元素，允许 dst 与输入别名）。 */
void nex_par_ntt_pointwise(uint32_t *dst, const uint32_t *lhs,
        const uint32_t *rhs, size_t len, const ntt_mod_ty *mod,
        nex_thread_pool_ty *pool);

#ifdef __cplusplus
}
#endif

#endif /* NEX_PARALLEL_NTT_PAR_H */
