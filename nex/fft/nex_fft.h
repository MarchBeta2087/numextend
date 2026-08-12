#ifndef NEX_FFT_H
#define NEX_FFT_H

#include <stddef.h>
#include <stdint.h>

/*
 * nex_fft：浮点复数 FFT 核心模块（设计文档 §4.3"浮点复数 FFT"）。
 *
 * 本模块是 bin / dec 乘法分派共用的只读公共算法模块：只操作复系数数组
 * （交错 double：coeffs[2i] = 实部、coeffs[2i+1] = 虚部），不依赖任何
 * bigint 类型，无动态分配、无全局可变状态（设计文档 §3.3）。
 *
 * 关键约定：
 *   - 变换长度 N = 2^log2n（log2n ≤ 30）；正变换用 DIF（频域抽取，
 *     Gentleman–Sande）蝶形、逆变换用 DIT（时域抽取，Cooley–Tukey）蝶形，
 *     正逆配对后输入输出均为自然序，无需位反转（设计文档 §4.3）；
 *   - 逆变换末尾乘以 1/N 归一化；
 *   - 精度：双精度 53 位尾数。用于大整数乘法时以 8-bit 节为默认
 *     （系数上界 C ≤ L·(2^8−1)²，N 可达 ~2^34 点仍可精确舍入，
 *     论证见设计文档 §4.3）；增大节位宽须调用方自行评估舍入安全界。
 *
 * 循环卷积配方（调用方自备缓冲，长度 N = 2^log2n，交错 double）：
 *     fft_forward(buf_a, log2n);
 *     fft_forward(buf_b, log2n);
 *     fft_pointwise_mul(buf_a, buf_a, buf_b, n);
 *     fft_inverse(buf_a, log2n);   // buf_a 为循环卷积结果
 */

/* FFT 模块错误码 */
typedef enum {
    FFT_OK_E = 0,       // 成功
    FFT_ERR_INVALID_E   // 非法参数（空指针、log2n 越界等）
} fft_err_ty;

/*
 * brief: 就地正变换（DIF 蝶形；自然序输入 → 位反转输出）
 * param: coeffs  长度 N = 2^log2n 的复系数数组（交错 double，就地改写）
 * param: log2n   变换长度对数，须满足 log2n ≤ 30
 */
fft_err_ty fft_forward(double *coeffs, uint32_t log2n);

/*
 * brief: 就地逆变换（DIT 蝶形；位反转输入 → 自然序输出，含 1/N 归一化）
 */
fft_err_ty fft_inverse(double *coeffs, uint32_t log2n);

/*
 * brief: 频域点乘 dst = lhs·rhs（逐元素复乘）
 * note: dst 允许与 lhs / rhs 别名
 */
fft_err_ty fft_pointwise_mul(double *dst, const double *lhs,
        const double *rhs, size_t len);

#endif /* NEX_FFT_H */
