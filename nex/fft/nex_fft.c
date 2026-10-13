/*
 * nex_fft.c：浮点复数 FFT 核心实现（设计文档 §4.3"浮点复数 FFT"）。
 *
 * 职责：DIF 正变换 / DIT 逆变换（正逆配对免位反转）、频域点乘。
 * 只操作交错 double 复系数数组，不依赖 bigint 类型；无动态分配，
 * 全部函数无全局可变状态（§3.3 线程安全）。
 *
 * 正确性要点（随实现维护）：
 *   - 正变换 wlen = e^(−2πi/len)、逆变换 wlen = e^(+2πi/len)，配合
 *     DIF/DIT 配对，整体输入输出均自然序（设计文档 §4.3"位反转与蝶形"）；
 *   - w 链逐步复乘推进旋转因子，误差 O(N·ε) 量级；大整数乘法场景以
 *     8-bit 节为主（系数 C ≤ L·(2^8−1)²，N 可达 ~2^34 点仍可精确
 *     舍入，论证见设计文档 §4.3），远小于 0.5 的舍入阈值；
 *   - M_PI 非 C99 标准宏（POSIX），自备 NEX_FFT_PI 保证可移植。
 */

#include "nex/fft/nex_fft.h"

#include <math.h>

#define NEX_FFT_MAX_LOG2N 30U
#define NEX_FFT_PI 3.14159265358979323846

/* ------------------------------------------------------------------ */
/* 对外 API：变换                                                      */
/* ------------------------------------------------------------------ */

fft_err_ty fft_forward(double *coeffs, uint32_t log2n)
{
    if ((coeffs == NULL) || (log2n > NEX_FFT_MAX_LOG2N)) {
        return FFT_ERR_INVALID_E;
    }
    const uint32_t n = 1U << log2n;

    for (uint32_t s = log2n; s >= 1U; s--) {
        const uint32_t len = 1U << s;
        const uint32_t half = len >> 1U;
        /* 本阶段旋转因子 wlen = e^(−2πi/len)；w 从 1 逐步推进 */
        const double wlen_re = cos(-2.0 * NEX_FFT_PI / (double)len);
        const double wlen_im = sin(-2.0 * NEX_FFT_PI / (double)len);
        double w_re = 1.0;
        double w_im = 0.0;
        for (uint32_t k = 0U; k < half; k++) {
            for (uint32_t j = k; j < n; j += len) {
                const double u_re = coeffs[2U * j];
                const double u_im = coeffs[2U * j + 1U];
                const double v_re = coeffs[2U * (j + half)];
                const double v_im = coeffs[2U * (j + half) + 1U];
                /* DIF 蝶形：sum 与 (u−v)·w */
                coeffs[2U * j] = u_re + v_re;
                coeffs[2U * j + 1U] = u_im + v_im;
                const double d_re = u_re - v_re;
                const double d_im = u_im - v_im;
                coeffs[2U * (j + half)] = d_re * w_re - d_im * w_im;
                coeffs[2U * (j + half) + 1U] = d_re * w_im + d_im * w_re;
            }
            const double nw_re = w_re * wlen_re - w_im * wlen_im;
            const double nw_im = w_re * wlen_im + w_im * wlen_re;
            w_re = nw_re;
            w_im = nw_im;
        }
    }
    return FFT_OK_E;
}

fft_err_ty fft_inverse(double *coeffs, uint32_t log2n)
{
    if ((coeffs == NULL) || (log2n > NEX_FFT_MAX_LOG2N)) {
        return FFT_ERR_INVALID_E;
    }
    const uint32_t n = 1U << log2n;

    for (uint32_t s = 1U; s <= log2n; s++) {
        const uint32_t len = 1U << s;
        const uint32_t half = len >> 1U;
        /* 逆变换用根的逆：wlen = e^(+2πi/len) */
        const double wlen_re = cos(2.0 * NEX_FFT_PI / (double)len);
        const double wlen_im = sin(2.0 * NEX_FFT_PI / (double)len);
        double w_re = 1.0;
        double w_im = 0.0;
        for (uint32_t k = 0U; k < half; k++) {
            for (uint32_t j = k; j < n; j += len) {
                const double u_re = coeffs[2U * j];
                const double u_im = coeffs[2U * j + 1U];
                const double wv_re = coeffs[2U * (j + half)] * w_re
                        - coeffs[2U * (j + half) + 1U] * w_im;
                const double wv_im = coeffs[2U * (j + half)] * w_im
                        + coeffs[2U * (j + half) + 1U] * w_re;
                /* DIT 蝶形：u ± v·w */
                coeffs[2U * j] = u_re + wv_re;
                coeffs[2U * j + 1U] = u_im + wv_im;
                coeffs[2U * (j + half)] = u_re - wv_re;
                coeffs[2U * (j + half) + 1U] = u_im - wv_im;
            }
            const double nw_re = w_re * wlen_re - w_im * wlen_im;
            const double nw_im = w_re * wlen_im + w_im * wlen_re;
            w_re = nw_re;
            w_im = nw_im;
        }
    }
    /* 归一化：1/N */
    const double inv_n = 1.0 / (double)n;
    for (uint32_t i = 0U; i < n; i++) {
        coeffs[2U * i] *= inv_n;
        coeffs[2U * i + 1U] *= inv_n;
    }
    return FFT_OK_E;
}

fft_err_ty fft_pointwise_mul(double *dst, const double *lhs,
        const double *rhs, size_t len)
{
    if ((dst == NULL) || (lhs == NULL) || (rhs == NULL)) {
        return FFT_ERR_INVALID_E;
    }
    for (size_t i = 0U; i < len; i++) {
        const double lr = lhs[2U * i];
        const double li = lhs[2U * i + 1U];
        const double rr = rhs[2U * i];
        const double ri = rhs[2U * i + 1U];
        dst[2U * i] = lr * rr - li * ri;
        dst[2U * i + 1U] = lr * ri + li * rr;
    }
    return FFT_OK_E;
}
