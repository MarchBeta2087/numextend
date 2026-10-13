/* 单元测试：bigint_bin 多模数 CRT NTT 乘法集成（设计文档 §4.3） */
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

/* 确定性 LCG（与 test_ntt 同参数，便于复现） */
static uint32_t g_rng = 0x9e3779b9U;
static uint32_t next_u32(void)
{
    g_rng = g_rng * 1664525U + 1013904223U;
    return g_rng;
}

/*
 * brief: 生成长度恰为 len 的随机幅值（最高肢置最高位保证精确长度）
 */
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
 * brief: 强制 NTT 乘法结果与 schoolbook 参考一致
 */
static void check_mul_match(size_t la, size_t lb)
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
    m.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
    m.params.multi_moduli_crt_ntt.mod_count = 0;
    CHECK(bigint_bin_mul_ex(&got, &a, &b, &m) == BIGINT_OK_E);
    if (bigint_bin_cmp(&ref, &got) != 0) {
        printf("FAIL mul mismatch la=%u lb=%u\n", (unsigned)la, (unsigned)lb);
        g_fail++;
    }
    /* 显式 mod_count = 2 与默认一致 */
    m.params.multi_moduli_crt_ntt.mod_count = 2;
    CHECK(bigint_bin_mul_ex(&got, &a, &b, &m) == BIGINT_OK_E);
    if (bigint_bin_cmp(&ref, &got) != 0) {
        printf("FAIL mul mod2 mismatch la=%u lb=%u\n",
                (unsigned)la, (unsigned)lb);
        g_fail++;
    }

    bigint_bin_free(&got);
    bigint_bin_free(&ref);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

/*
 * brief: 尺寸扫描：覆盖变换长度边界（N = 2^log2n 与 ±1）与分节边界
 */
static void test_sizes(void)
{
    static const size_t lens[] = {
        1U, 2U, 3U, 4U, 5U, 7U, 8U, 9U, 15U, 16U, 17U, 31U, 32U, 33U,
        63U, 64U, 65U, 127U, 128U, 129U, 255U, 256U, 257U, 511U, 512U,
        513U, 1023U, 1024U, 1025U, 2047U, 2048U
    };
    const size_t count = sizeof(lens) / sizeof(lens[0]);
    for (size_t i = 0U; i < count; i++) {
        for (size_t j = 0U; j < count; j += 7U) {
            check_mul_match(lens[i], lens[j]);
        }
    }
    /* 非平衡组合 */
    check_mul_match(2U, 2048U);
    check_mul_match(2048U, 2U);
    check_mul_match(1U, 2048U);
    check_mul_match(2048U, 2048U);
}

/*
 * brief: 零、一、符号与别名
 */
static void test_edge_cases(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty r;
    bigint_bin_ty zero;
    bigint_bin_ty neg;
    bigint_bin_ty pos;
    bigint_mul_method_ty m;
    m.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
    m.params.multi_moduli_crt_ntt.mod_count = 0;

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
    /* 零 × 单肢（s = 1，变换长度须容纳 2 节——修复 ASan 堆越界） */
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

    /* 别名：dst 与操作数同一对象（乘法内部经 tmp 规避） */
    CHECK(bigint_bin_mul_ex(&a, &a, &b, &m) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&a, &pos) == 0);

    bigint_bin_free(&pos);
    bigint_bin_free(&neg);
    bigint_bin_free(&zero);
    bigint_bin_free(&r);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

/*
 * brief: 错误码：mod_count 非 0/2 返回 UNSUPPORTED
 */
static void test_method_errors(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty r;
    bigint_mul_method_ty m;
    CHECK(rand_bin(&a, 64U) == BIGINT_OK_E);
    CHECK(rand_bin(&b, 64U) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&r) == BIGINT_OK_E);

    m.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
    m.params.multi_moduli_crt_ntt.mod_count = 3;
    CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_UNSUPPORTED_E);
    m.params.multi_moduli_crt_ntt.mod_count = 1;
    CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_UNSUPPORTED_E);

    bigint_bin_free(&r);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

/* ------------------------------------------------------------------ */
/* Toom-3 专项（设计文档 §4.2.4；含稀疏值 / 10 的幂——值相关缺陷捕手）   */
/* ------------------------------------------------------------------ */

static void check_toom_match(const bigint_bin_ty *a, const bigint_bin_ty *b)
{
    bigint_bin_ty ref;
    bigint_bin_ty got;
    bigint_mul_method_ty m;
    bigint_bin_init(&ref);
    bigint_bin_init(&got);
    m.algo = BIGINT_MUL_SCHOOLBOOK_E;
    m.params.schoolbook.reserved = 0;
    CHECK(bigint_bin_mul_ex(&ref, a, b, &m) == BIGINT_OK_E);
    m.algo = BIGINT_MUL_TOOM_COOK_E;
    m.params.toom_cook.k = 3;
    m.params.toom_cook.cutoff = 0;
    CHECK(bigint_bin_mul_ex(&got, a, b, &m) == BIGINT_OK_E);
    if (bigint_bin_cmp(&ref, &got) != 0) {
        printf("FAIL toom mismatch la=%u lb=%u\n",
                (unsigned)a->len, (unsigned)b->len);
        g_fail++;
    }
    bigint_bin_free(&got);
    bigint_bin_free(&ref);
}

/* 构造 10^k（十进制幂，二进制低位稀疏——值相关缺陷高发区） */
static void pow10_bin(bigint_bin_ty *v, size_t k)
{
    bigint_bin_ty ten;
    bigint_bin_init(&ten);
    bigint_bin_from_u64(&ten, 10U);
    bigint_bin_from_u64(v, 1U);
    for (size_t i = 0U; i < k; i++) {
        bigint_bin_ty t;
        bigint_bin_init(&t);
        bigint_bin_mul(&t, v, &ten);
        bigint_bin_move(v, &t);
    }
    bigint_bin_free(&ten);
}

static void test_toom3(void)
{
    /* 随机尺寸：覆盖 Toom 区间（256..16384）与递归边界 */
    static const size_t lens[] = { 100U, 256U, 257U, 300U, 319U, 320U,
            479U, 500U, 957U, 1000U, 2048U, 4096U, 8192U };
    for (size_t i = 0U; i < sizeof(lens) / sizeof(lens[0]); i++) {
        bigint_bin_ty a;
        bigint_bin_ty b;
        CHECK(rand_bin(&a, lens[i]) == BIGINT_OK_E);
        CHECK(rand_bin(&b, (lens[i] + 5U) / 2U) == BIGINT_OK_E);
        check_toom_match(&a, &b);
        check_toom_match(&b, &a);
        check_toom_match(&a, &a);  /* 平方 */
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }
    /* 稀疏值：10 的幂平方与相乘（低半全零，回归 avm1/零分支缺陷） */
    for (size_t k = 1000U; k <= 9000U; k += 1000U) {
        bigint_bin_ty a;
        bigint_bin_ty b;
        bigint_bin_init(&a);
        bigint_bin_init(&b);
        pow10_bin(&a, k);
        pow10_bin(&b, k);
        check_toom_match(&a, &b);   /* 10^k 平方 */
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }
    {
        /* 10^9216（957 肢，低 288 肢全零）平方——历史缺陷精确复现用例 */
        bigint_bin_ty a;
        bigint_bin_init(&a);
        pow10_bin(&a, 9216U);
        CHECK(a.len == 957U);
        check_toom_match(&a, &a);
        bigint_bin_free(&a);
    }
}

int main(void)
{
    test_sizes();
    test_edge_cases();
    test_method_errors();
    test_toom3();
    if (g_fail == 0) {
        printf("ntt_mul: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
