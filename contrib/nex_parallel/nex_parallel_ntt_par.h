#ifndef NEX_PARALLEL_NTT_PAR_H
#define NEX_PARALLEL_NTT_PAR_H

#include <stddef.h>
#include <stdint.h>

#include "nex/ntt/nex_ntt.h"
#include "nex_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * contrib 内的阶段级并行 NTT（与 nex_ntt 的 DIF/DIT + Montgomery 实现
 * 逐位一致；调用方负责参数合法性与池生命周期）。
 */
void nex_par_ntt_forward(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool);
void nex_par_ntt_inverse(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod, nex_thread_pool_ty *pool);
void nex_par_ntt_pointwise(uint32_t *dst, const uint32_t *lhs,
        const uint32_t *rhs, size_t len, const ntt_mod_ty *mod,
        nex_thread_pool_ty *pool);

#ifdef __cplusplus
}
#endif

#endif /* NEX_PARALLEL_NTT_PAR_H */
