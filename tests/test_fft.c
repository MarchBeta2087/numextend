/* 单元测试：nex_fft 浮点复数 FFT 核心模块（设计文档 §4.3） */
#include "nex/fft/nex_fft.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

/* 确定性 LCG */
static uint32_t g_rng = 0x13579bdfU;
static uint32_t next_u32(void)
{
    g_rng = g_rng * 1664525U + 1013904223U;
    return g_rng;
}

/* 朴素循环卷积（实数输入，双精度输出），作 NTT/FFT 卷积对拍基准 */
static void naive_cyclic_conv(double *out, const double *a, const double *b,
        size_t n)
{
    for (size_t i = 0U; i < n; i++) {
        out[i] = 0.0;
    }
    for (size_t i = 0U; i < n; i++) {
        for (size_t j = 0U; j < n; j++) {
            out[(i + j) % n] += a[i] * b[j];
        }
    }
}

static void test_roundtrip_one(uint32_t log2n)
{
    const uint32_t n = 1U << log2n;
    double *buf = (double *)malloc(2U * n * sizeof(double));
    double *orig = (double *)malloc(2U * n * sizeof(double));
    CHECK((buf != NULL) && (orig != NULL));
    if ((buf == NULL) || (orig == NULL)) {
        free(buf);
        free(orig);
        return;
    }
    for (uint32_t i = 0U; i < n; i++) {
        orig[2U * i] = (double)(next_u32() % 1000U);
        orig[2U * i + 1U] = (double)(next_u32() % 1000U);
        buf[2U * i] = orig[2U * i];
        buf[2U * i + 1U] = orig[2U * i + 1U];
    }
    CHECK(fft_forward(buf, log2n) == FFT_OK_E);
    CHECK(fft_inverse(buf, log2n) == FFT_OK_E);
    for (uint32_t i = 0U; i < 2U * n; i++) {
        if (fabs(buf[i] - orig[i]) > 1e-6) {
            printf("FAIL roundtrip log2n=%u idx=%u: got %.9f want %.9f\n",
                    log2n, i, buf[i], orig[i]);
            g_fail++;
            break;
        }
    }
    free(buf);
    free(orig);
}

static void test_roundtrip(void)
{
    test_roundtrip_one(0U);
    test_roundtrip_one(1U);
    test_roundtrip_one(2U);
    test_roundtrip_one(4U);
    test_roundtrip_one(8U);
    test_roundtrip_one(10U);
}

static void test_convolution_one(uint32_t log2n)
{
    const uint32_t n = 1U << log2n;
    double *a = (double *)malloc(2U * n * sizeof(double));
    double *b = (double *)malloc(2U * n * sizeof(double));
    double *ar = (double *)malloc(n * sizeof(double));
    double *br = (double *)malloc(n * sizeof(double));
    double *want = (double *)malloc(n * sizeof(double));
    CHECK((a != NULL) && (b != NULL) && (ar != NULL) && (br != NULL)
            && (want != NULL));
    if ((a == NULL) || (b == NULL) || (ar == NULL) || (br == NULL)
            || (want == NULL)) {
        free(a);
        free(b);
        free(ar);
        free(br);
        free(want);
        return;
    }
    for (uint32_t i = 0U; i < n; i++) {
        ar[i] = (double)(next_u32() % 1000U);
        br[i] = (double)(next_u32() % 1000U);
        a[2U * i] = ar[i];
        a[2U * i + 1U] = 0.0;
        b[2U * i] = br[i];
        b[2U * i + 1U] = 0.0;
    }
    /* 对拍基准须在变换前计算：fft_forward 就地改写 */
    naive_cyclic_conv(want, ar, br, n);
    CHECK(fft_forward(a, log2n) == FFT_OK_E);
    CHECK(fft_forward(b, log2n) == FFT_OK_E);
    CHECK(fft_pointwise_mul(a, a, b, n) == FFT_OK_E);
    CHECK(fft_inverse(a, log2n) == FFT_OK_E);
    for (uint32_t i = 0U; i < n; i++) {
        if (fabs(a[2U * i] - want[i]) > 1e-6) {
            printf("FAIL conv log2n=%u idx=%u: got %.9f want %.9f\n",
                    log2n, i, a[2U * i], want[i]);
            g_fail++;
            break;
        }
        /* 实输入的卷积虚部应接近零 */
        if (fabs(a[2U * i + 1U]) > 1e-6) {
            printf("FAIL conv im log2n=%u idx=%u: %.9f\n",
                    log2n, i, a[2U * i + 1U]);
            g_fail++;
            break;
        }
    }
    free(a);
    free(b);
    free(ar);
    free(br);
    free(want);
}

static void test_convolution(void)
{
    test_convolution_one(2U);
    test_convolution_one(4U);
    test_convolution_one(8U);
    test_convolution_one(10U);
}

static void test_invalid_args(void)
{
    double buf[16] = { 0.0 };
    CHECK(fft_forward(NULL, 3U) == FFT_ERR_INVALID_E);
    CHECK(fft_inverse(NULL, 3U) == FFT_ERR_INVALID_E);
    CHECK(fft_forward(buf, 31U) == FFT_ERR_INVALID_E);
    CHECK(fft_inverse(buf, 31U) == FFT_ERR_INVALID_E);
    CHECK(fft_pointwise_mul(NULL, buf, buf, 8U) == FFT_ERR_INVALID_E);
    CHECK(fft_pointwise_mul(buf, NULL, buf, 8U) == FFT_ERR_INVALID_E);
    CHECK(fft_pointwise_mul(buf, buf, NULL, 8U) == FFT_ERR_INVALID_E);
    CHECK(fft_forward(buf, 3U) == FFT_OK_E);
    CHECK(fft_pointwise_mul(buf, buf, buf, 8U) == FFT_OK_E);
    CHECK(fft_inverse(buf, 3U) == FFT_OK_E);
}

int main(void)
{
    test_roundtrip();
    test_convolution();
    test_invalid_args();
    if (g_fail == 0) {
        printf("fft: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
