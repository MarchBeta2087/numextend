/* 单元测试：bigint_bin ↔ bigint_dec 分治互转（设计文档 §4.2.6、§13 #10） */
#include "nex/bigint/nex_bigint_conv.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

static uint32_t g_rng = 0xabcdef01U;
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
 * brief: 随机幅值双向往返：bin→dec→bin 与 dec→bin→dec 均须还原
 */
static void check_roundtrip(size_t bin_len)
{
    bigint_bin_ty a;
    bigint_bin_ty back;
    bigint_dec_ty d;
    bigint_dec_ty d2;
    CHECK(rand_bin(&a, bin_len) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&back) == BIGINT_OK_E);
    CHECK(bigint_dec_init(&d) == BIGINT_OK_E);
    CHECK(bigint_dec_init(&d2) == BIGINT_OK_E);

    CHECK(bigint_conv_bin_to_dec(&d, &a) == BIGINT_OK_E);
    CHECK(bigint_conv_dec_to_bin(&back, &d) == BIGINT_OK_E);
    if (bigint_bin_cmp(&a, &back) != 0) {
        printf("FAIL b2d2b roundtrip bin_len=%u\n", (unsigned)bin_len);
        g_fail++;
    }
    CHECK(bigint_conv_dec_to_bin(&back, &d) == BIGINT_OK_E);  /* 复用别名 */
    CHECK(bigint_conv_bin_to_dec(&d2, &back) == BIGINT_OK_E);
    if (bigint_dec_cmp(&d, &d2) != 0) {
        printf("FAIL d2b2d roundtrip bin_len=%u\n", (unsigned)bin_len);
        g_fail++;
    }

    bigint_dec_free(&d2);
    bigint_dec_free(&d);
    bigint_bin_free(&back);
    bigint_bin_free(&a);
}

static void test_roundtrip_sizes(void)
{
    /* 覆盖朴素 / 分治边界与多级递归：dec 阈值 64 肢 ≈ 577 位 */
    static const size_t lens[] = {
        1U, 2U, 7U, 15U, 16U, 17U, 31U, 32U, 33U, 63U, 64U, 65U, 127U,
        128U, 129U, 191U, 192U, 193U, 255U, 256U, 257U, 383U, 512U,
        513U, 767U, 768U, 1024U, 1535U, 1536U, 2000U
    };
    for (size_t i = 0U; i < sizeof(lens) / sizeof(lens[0]); i++) {
        check_roundtrip(lens[i]);
    }
}

static void test_edge_cases(void)
{
    bigint_bin_ty a;
    bigint_bin_ty back;
    bigint_dec_ty d;
    bigint_bin_init(&a);
    bigint_bin_init(&back);
    bigint_dec_init(&d);

    /* 零 */
    CHECK(bigint_conv_bin_to_dec(&d, &a) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&d));
    CHECK(bigint_conv_dec_to_bin(&back, &d) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&back));

    /* 一 */
    CHECK(bigint_bin_from_u64(&a, 1U) == BIGINT_OK_E);
    CHECK(bigint_conv_bin_to_dec(&d, &a) == BIGINT_OK_E);
    CHECK(bigint_conv_dec_to_bin(&back, &d) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&a, &back) == 0);

    /* 负号传播 */
    CHECK(rand_bin(&a, 100U) == BIGINT_OK_E);
    CHECK(bigint_bin_neg(&a) == BIGINT_OK_E);
    CHECK(bigint_conv_bin_to_dec(&d, &a) == BIGINT_OK_E);
    CHECK(bigint_dec_sign(&d) == BIGINT_SIGN_NEG_E);
    CHECK(bigint_conv_dec_to_bin(&back, &d) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&a, &back) == 0);

    /* 稀疏数（低位全零）：分治低位段为零路径（300 肢 > 256 阈值） */
    CHECK(rand_bin(&a, 300U) == BIGINT_OK_E);
    for (size_t i = 0U; i < 260U; i++) {
        a.limbs[i] = 0U;
    }
    CHECK(bigint_conv_bin_to_dec(&d, &a) == BIGINT_OK_E);
    CHECK(bigint_conv_dec_to_bin(&back, &d) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&a, &back) == 0);

    bigint_dec_free(&d);
    bigint_bin_free(&back);
    bigint_bin_free(&a);
}

int main(void)
{
    test_roundtrip_sizes();
    test_edge_cases();
    if (g_fail == 0) {
        printf("bigint_conv: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
