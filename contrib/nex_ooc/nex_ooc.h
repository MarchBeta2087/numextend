#ifndef NEX_OOC_H
#define NEX_OOC_H

#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/bin/nex_bigint_bin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nex_ooc：out-of-core 分块乘法原型（P1b-OOC，issue #104）。
 *
 * 把操作数视为「肢文件」（小端 32-bit 肢序列，无头），按块长 L 切分：
 *
 *   a = Σ a_i·B^(iL)，b = Σ b_j·B^(jL)，B = 2^32
 *   a·b = Σ_k B^(kL)·( Σ_{i+j=k} a_i·b_j )
 *
 * 输出按 k 升序生成：块 k 的累加器只保留 2L+2 肢，算完立即写盘，高位
 * 作为进位带入块 k+1。因此**峰值内存 O(L)，与操作数规模无关**，代价是
 * 每个 a_i / b_j 块被重复读取（块对总数 na·nb，I/O 约 O(n·nb)）。
 *
 * 与内存内算法（`bigint_bin_mul` / `contrib/nex_bigint_block`）逐位一致；
 * 块对乘法直接复用核心 AUTO 分派（块内为 Karatsuba/Toom-3/CRT NTT）。
 *
 * 限制：M1 只用单线程顺序 I/O，未做块缓存、并行块对、块级 FFT；这些是
 * issue #104 的 M3 项。10^10 位的块数约 32（L=2^25 时），I/O 量约
 * 数百 GB，需 M2 实测磁盘带宽。
 */

/* 默认块长：2^23 肢（33.5 MB）≈ 2.5×10^6 十进制位/块 */
#define NEX_OOC_DEFAULT_BLOCK_LIMBS (1U << 23U)
/* 上限与 contrib/nex_bigint_block 一致（单块直接 NTT 的 s ≤ 2^27） */
#define NEX_OOC_MAX_BLOCK_LIMBS (1U << 26U)

/* 错误码：本 contrib 模块自带 I/O 错误，避免污染核心 bigint_err_ty */
typedef enum {
    NEX_OOC_OK_E = 0,
    NEX_OOC_ERR_INVALID_E,     /* 空指针 / 非法块长 / 打不开文件 */
    NEX_OOC_ERR_OOM_E,         /* 内存不足 */
    NEX_OOC_ERR_IO_E,          /* 读/写/定位/截断失败 */
    NEX_OOC_ERR_UNSUPPORTED_E  /* 块对乘法返回 UNSUPPORTED（规模越界） */
} nex_ooc_err_ty;

typedef struct {
    size_t block_limbs;    /* 块长（肢）；0 → NEX_OOC_DEFAULT_BLOCK_LIMBS */
    const char *scratch_dir; /* 临时文件目录；NULL → 当前目录 */
    int keep_temps;        /* 非零：保留临时文件（调试用） */
} nex_ooc_opts_ty;

typedef struct {
    size_t block_limbs;    /* 实际使用的块长 */
    size_t blocks_a;       /* a 的块数 */
    size_t blocks_b;       /* b 的块数 */
    size_t block_pairs;    /* 实际执行的块对乘法数 */
    uint64_t bytes_read;   /* 从 a/b 读入的字节数 */
    uint64_t bytes_written;/* 写入结果的字节数（含尾部裁剪前的冗余） */
    uint64_t result_limbs; /* 结果肢数（裁剪后） */
    double seconds;        /* 挂钟时间 */
} nex_ooc_stats_ty;

/*
 * brief: 文件到文件的乘法（操作数为纯肢体文件，不含符号/长度头）
 * param: dst_path 结果文件路径（覆盖写；小端 32-bit 肢体，无头）
 * param: a_path / a_limbs 操作数 a 的路径与肢数
 * param: b_path / b_limbs 操作数 b 的路径与肢数
 * param: opts 选项，可为 NULL
 * param: stats 统计输出，可为 NULL
 * return: NEX_OOC_OK_E 成功；否则见 nex_ooc_err_ty
 * note: 结果取幅值（符号由调用方处理）；任一操作数为零时生成空文件
 */
nex_ooc_err_ty nex_ooc_mul_files(const char *dst_path,
        const char *a_path, size_t a_limbs,
        const char *b_path, size_t b_limbs,
        const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats);

/*
 * brief: 内存大整数乘法，内部经临时文件走 out-of-core 路径
 * param: dst 结果；lhs / rhs 操作数（允许别名，符号按常规规则）
 * param: opts / stats 同上
 * return: NEX_OOC_OK_E 成功；否则见 nex_ooc_err_ty
 * note: 仅用于验证与中小规模使用；大输入本身已在内存中，省不了内存
 */
nex_ooc_err_ty nex_ooc_bin_mul(bigint_bin_ty *dst,
        const bigint_bin_ty *lhs, const bigint_bin_ty *rhs,
        const nex_ooc_opts_ty *opts, nex_ooc_stats_ty *stats);

/*
 * brief: 峰值内存的经验估计（字节），用于选定块长
 * param: block_limbs 块长；0 表示默认块长
 * return: 估计的峰值字节数（工作缓冲区 + NTT 内部估算）
 */
size_t nex_ooc_estimate_peak_bytes(size_t block_limbs);

/*
 * brief: 错误码名称（用于日志/基准输出）
 */
const char *nex_ooc_strerror(nex_ooc_err_ty err);

#ifdef __cplusplus
}
#endif

#endif /* NEX_OOC_H */
