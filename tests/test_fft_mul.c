/* 单元测试：bigint_bin 浮点复数 FFT 乘法集成（设计文档 §4.3） */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

static uint32_t g_rng = 0x2468ace0U;
static uint32_t next_u32(void)
{
    g_rng = g_rng * 1664525U + 1013904223U;
    return g_rng;
}

static bigint_err_ty rand_bin(bigint_bin_ty *val, size_t len)
{
    bigint_err_ty err = bigint_bin_init_cap(val, len);
    if (err != BIGINT_OK_E) {
        return err;
    }
    for (size_t i = 0U; i < len; i++) {
        val->limbs[i] = next_u32();
    }
    if (len > 0U) {
        val->limbs[len - 1U] |= 0x80000000U;
    }
    val->len = len;
    val->sign = BIGINT_SIGN_POS_E;
    return BIGINT_OK_E;
}

/*
 * brief: 强制 FFT（指定节位宽）乘法结果与 schoolbook 参考一致
 */
static void check_mul_match(size_t la, size_t lb, uint32_t chunk_bits)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty ref;
    bigint_bin_ty got;
    bigint_mul_method_ty m;
    CHECK(rand_bin(&a, la) == BIGINT_OK_E);
    CHECK(rand_bin(&b, lb) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&ref) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&got) == BIGINT_OK_E);

    m.algo = BIGINT_MUL_SCHOOLBOOK_E;
    m.params.schoolbook.reserved = 0;
    CHECK(bigint_bin_mul_ex(&ref, &a, &b, &m) == BIGINT_OK_E);
    m.algo = BIGINT_MUL_FLOAT_COMPLEX_FFT_E;
    m.params.float_complex_fft.chunk_bits = chunk_bits;
    CHECK(bigint_bin_mul_ex(&got, &a, &b, &m) == BIGINT_OK_E);
    if (bigint_bin_cmp(&ref, &got) != 0) {
        printf("FAIL fft mismatch la=%u lb=%u bits=%u\n", (unsigned)la,
                (unsigned)lb, chunk_bits);
        g_fail++;
    }

    bigint_bin_free(&got);
    bigint_bin_free(&ref);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

/*
 * brief: 尺寸扫描：覆盖 8/16-bit 节边界、变换长度边界与 16-bit 舍入
 *        安全上限（S ≤ 1024 附近，多轮压力验证舍入精确性）
 */
static void test_sizes(void)
{
    static const size_t lens[] = {
        1U, 2U, 3U, 4U, 7U, 8U, 15U, 16U, 17U, 31U, 32U, 33U, 63U, 64U,
        127U, 128U, 255U, 256U, 257U, 511U, 512U
    };
    const size_t count = sizeof(lens) / sizeof(lens[0]);
    for (size_t i = 0U; i < count; i++) {
        for (size_t j = 0U; j < count; j += 7U) {
            check_mul_match(lens[i], lens[j], 0U);
        }
    }
    /* 16-bit 节显式强制 + 舍入边界压力（S ≤ 1024 边界内多轮） */
    for (int t = 0; t < 40; t++) {
        check_mul_match(512U, 512U, 16U);
        check_mul_match(511U, 513U, 16U);
    }
    /* 8-bit 节显式强制：中小与大尺寸 */
    check_mul_match(512U, 512U, 8U);
    check_mul_match(1024U, 1024U, 8U);
    check_mul_match(2048U, 1024U, 8U);
    /* 自动（>1024 长度和 → 8-bit） */
    check_mul_match(1024U, 1024U, 0U);
    check_mul_match(2048U, 2048U, 0U);
    check_mul_match(4096U, 2048U, 0U);
}

static void test_edge_cases(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty r;
    bigint_bin_ty zero;
    bigint_bin_ty neg;
    bigint_bin_ty pos;
    bigint_mul_method_ty m;
    m.algo = BIGINT_MUL_FLOAT_COMPLEX_FFT_E;
    m.params.float_complex_fft.chunk_bits = 0;

    CHECK(rand_bin(&a, 64U) == BIGINT_OK_E);
    CHECK(rand_bin(&b, 64U) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&r) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&zero) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&neg) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&pos) == BIGINT_OK_E);

    /* 零 × 任意 = 零 */
    CHECK(bigint_bin_mul_ex(&r, &a, &zero, &m) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&r));
    CHECK(bigint_bin_mul_ex(&r, &zero, &a, &m) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&r));
    /* 零 × 单肢（s = 1，变换长度须容纳单侧节数——修复 ASan 堆越界） */
    {
        bigint_bin_ty one;
        CHECK(rand_bin(&one, 1U) == BIGINT_OK_E);
        CHECK(bigint_bin_mul_ex(&r, &zero, &one, &m) == BIGINT_OK_E);
        CHECK(bigint_bin_is_zero(&r));
        CHECK(bigint_bin_mul_ex(&r, &one, &zero, &m) == BIGINT_OK_E);
        CHECK(bigint_bin_is_zero(&r));
        bigint_bin_free(&one);
    }

    /* 负号：−a × b == −(a × b) */
    CHECK(bigint_bin_copy(&neg, &a) == BIGINT_OK_E);
    CHECK(bigint_bin_neg(&neg) == BIGINT_OK_E);
    CHECK(bigint_bin_mul_ex(&pos, &a, &b, &m) == BIGINT_OK_E);
    CHECK(bigint_bin_mul_ex(&r, &neg, &b, &m) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&r, &pos) != 0);
    CHECK(bigint_bin_neg(&r) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&r, &pos) == 0);

    /* 别名 */
    CHECK(bigint_bin_mul_ex(&a, &a, &b, &m) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&a, &pos) == 0);

    bigint_bin_free(&pos);
    bigint_bin_free(&neg);
    bigint_bin_free(&zero);
    bigint_bin_free(&r);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

static void test_method_errors(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty r;
    bigint_mul_method_ty m;
    CHECK(rand_bin(&a, 64U) == BIGINT_OK_E);
    CHECK(rand_bin(&b, 64U) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&r) == BIGINT_OK_E);

    m.algo = BIGINT_MUL_FLOAT_COMPLEX_FFT_E;
    m.params.float_complex_fft.chunk_bits = 4;
    CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_INVALID_E);
    m.params.float_complex_fft.chunk_bits = 32;
    CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_INVALID_E);

    bigint_bin_free(&r);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

int main(void)
{
    test_sizes();
    test_edge_cases();
    test_method_errors();
    if (g_fail == 0) {
        printf("fft_mul: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
