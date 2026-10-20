/*
 * nex_opencl.h：contrib OpenCL 批量大整数实验（issue #98 P3）。
 *
 * 默认不构建；-DNEX_ENABLE_OPENCL=ON 时作为独立库 nex_opencl，
 * 不进 libnex / amalgam / 子集闭包，也不改核心 API/ABI。
 *
 * 数据表示：调用方提供「按元素连续存放」的无符号大整数数组，
 * 每个数固定 limbs 个 32-bit limb（小端），不足高位补零。
 * 例：1000 位十进制 ≈ 104 limb（3328 bit）。
 *
 * 当前覆盖：
 *   - nex_opencl_madd：out[i] = a[i]*b[i] + c[i]（逐元素）
 *   - nex_opencl_dot ：out = Σ a[i]*b[i]（一批点积）
 * 矩阵×向量复用 dot 的行形式，见 M2 计划（contrib/nex_opencl/README.md）。
 */
#ifndef NEX_OPENCL_H
#define NEX_OPENCL_H

#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nex_opencl_ctx nex_opencl_ctx_ty;

/* 创建上下文：编译针对 limbs 特化的 kernel；优先 GPU，其次 CPU。
 * 失败返回 NULL；err 非空时写入原因。 */
nex_opencl_ctx_ty *nex_opencl_create(size_t limbs, char *err, size_t err_size);

/* 释放上下文（可传 NULL）。 */
void nex_opencl_destroy(nex_opencl_ctx_ty *ctx);

const char *nex_opencl_device_name(const nex_opencl_ctx_ty *ctx);
const char *nex_opencl_error(const nex_opencl_ctx_ty *ctx);

/* 输入 limb 数（创建时指定）。 */
size_t nex_opencl_limbs(const nex_opencl_ctx_ty *ctx);

/* 结果 limb 数：2*limbs + 1（乘积 2*limbs，+1 保证进位/和余量）。 */
size_t nex_opencl_out_limbs(const nex_opencl_ctx_ty *ctx);

/* out[i] = a[i]*b[i] + c[i]，i ∈ [0, n)。
 * a/b/c 各 n*limbs 个 uint32_t；out 需 n*out_limbs 个。
 * 返回 0 成功，-1 失败（错误见 nex_opencl_error）。 */
int nex_opencl_madd(nex_opencl_ctx_ty *ctx,
        const uint32_t *a, const uint32_t *b, const uint32_t *c,
        uint32_t *out, size_t n);

/* out = Σ_{i<n} a[i]*b[i]，out 需 out_limbs 个 uint32_t。
 * n 上限 2^32（和的高位余量按 2*limbs+1 预留）。 */
int nex_opencl_dot(nex_opencl_ctx_ty *ctx,
        const uint32_t *a, const uint32_t *b,
        uint32_t *out, size_t n);

/* 便捷转换：把非负 bigint_bin 打包/解包为定长 limb 数组。
 * pack 时 v->len 必须 ≤ limbs；unpack 会裁剪前导零并置正/零符号。
 * unpack 要求 v 已用 bigint_bin_init()/init_cap() 初始化。
 * 返回 0 成功，-1 失败。 */
int nex_opencl_pack_bin(const bigint_bin_ty *v, uint32_t *out, size_t limbs);
int nex_opencl_unpack_bin(const uint32_t *limbs, size_t count,
        bigint_bin_ty *v);

#ifdef __cplusplus
}
#endif

#endif /* NEX_OPENCL_H */
