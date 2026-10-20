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
 * 矩阵×矩阵子集见 nex_opencl_matmul_subset（M2，contrib/nex_opencl/README.md）。
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

/* 当前设备名（创建时选定，生命周期同 ctx）。 */
const char *nex_opencl_device_name(const nex_opencl_ctx_ty *ctx);

/* 最近一次失败的原因字符串（成功调用后不保证清空）。 */
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

/* 子矩阵乘：C[i,j] = Σ_{p<k} A[rows[i],p] * B[p,cols[j]]。
 * A 为 m×k、B 为 k×n 的行主序定长 limb 数组（lda/ldb 为每行 limb 数），
 * C 为 nrows×ncols、每行 ldc 个 limb；输出每元素 out_limbs。
 * 内部按 tile 在 RAM/VRAM 间搬运，适合 4k×4k、只取部分行/列的场景。
 * 返回 0 成功，-1 失败。 */
int nex_opencl_matmul_subset(nex_opencl_ctx_ty *ctx,
        const uint32_t *A, size_t lda, const uint32_t *B, size_t ldb,
        const uint32_t *rows, size_t nrows,
        const uint32_t *cols, size_t ncols,
        uint32_t *C, size_t ldc, size_t k);

/* 稀疏矩阵（CSR / CSC）：索引按升序，values 为 nnz×limbs 的 limb 数组。
 * A 用 CSR（行主序），B 用 CSC（按列），便于 C[rows,cols] 的索引归并。 */
typedef struct {
    size_t rows;
    size_t nnz;
    const uint32_t *row_ptr;  /* rows+1 */
    const uint32_t *col_idx;  /* nnz，升序 */
    const uint32_t *values;   /* nnz × limbs */
} nex_opencl_csr_ty;

typedef struct {
    size_t cols;
    size_t nnz;
    const uint32_t *col_ptr;  /* cols+1 */
    const uint32_t *row_idx;  /* nnz，升序 */
    const uint32_t *values;   /* nnz × limbs */
} nex_opencl_csc_ty;

/* 稀疏子矩阵乘：只遍历 A 行非零与 B 列非零的交集。
 * 语义同 nex_opencl_matmul_subset；density=1 时建议走稠密接口。 */
int nex_opencl_matmul_sparse(nex_opencl_ctx_ty *ctx,
        const nex_opencl_csr_ty *A, const nex_opencl_csc_ty *B,
        const uint32_t *rows, size_t nrows,
        const uint32_t *cols, size_t ncols,
        uint32_t *C, size_t ldc);

/* 把非负 bigint_bin 打包为定长 limb 数组：v->len 必须 ≤ limbs，
 * 不足高位补零；out 需 limbs 个 uint32_t。返回 0 成功，-1 失败。 */
int nex_opencl_pack_bin(const bigint_bin_ty *v, uint32_t *out, size_t limbs);

/* 解包：从 count 个 limb 还原 bigint_bin，裁剪前导零并置正/零符号；
 * v 须已用 bigint_bin_init()/init_cap() 初始化。返回 0 成功，-1 失败。 */
int nex_opencl_unpack_bin(const uint32_t *limbs, size_t count,
        bigint_bin_ty *v);

#ifdef __cplusplus
}
#endif

#endif /* NEX_OPENCL_H */
