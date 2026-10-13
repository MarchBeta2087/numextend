/* 单元测试：bigint_bin 十进制字符串快速 I/O（设计文档 §13 方向落地） */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

/*
 * brief: from_str → to_str 往返：输出须与输入串逐字符一致
 */
static void check_str_roundtrip(const char *s)
{
    bigint_bin_ty v;
    bigint_bin_init(&v);
    if (bigint_bin_from_str(&v, s, 10, NULL) != BIGINT_OK_E) {
        printf("FAIL parse: %s\n", s);
        g_fail++;
        bigint_bin_free(&v);
        return;
    }
    size_t needed = 0U;
    CHECK(bigint_bin_to_str(&v, 10, NULL, 0, &needed) == BIGINT_OK_E);
    char *buf = (char *)malloc(needed);
    CHECK(buf != NULL);
    if (buf != NULL) {
        CHECK(bigint_bin_to_str(&v, 10, buf, needed, &needed) == BIGINT_OK_E);
        if (strcmp(buf, s) != 0) {
            printf("FAIL roundtrip len=%u\n%s\n%s\n", (unsigned)strlen(s),
                    s, buf);
            g_fail++;
        }
        free(buf);
    }
    bigint_bin_free(&v);
}

static void test_roundtrip(void)
{
    /* 9 位分组边界（dec 肢基 10^9）：9n、9n±1 位 */
    static const char *const cases[] = {
        "0", "1", "9", "10", "99", "100",
        "999999999", "1000000000", "1000000001", "9999999999",
        "123456789012345678", "1234567890123456789",
        "12345678901234567890",
        "999999999999999999999999999999",
        "1000000000000000000000000000000",
        "1234567890123456789012345678901234567890",
        "-12345678901234567890", "-1",
        "11111111111111111111111111111111111111111111111111111111"
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_str_roundtrip(cases[i]);
    }

    /* 大尺寸：1 万 / 5 万位（分治路径） */
    {
        const size_t n = 10000U;
        char *s = (char *)malloc(n + 1U);
        CHECK(s != NULL);
        if (s != NULL) {
            for (size_t i = 0U; i < n; i++) {
                s[i] = (char)('0' + (int)(i % 10U));
            }
            s[0] = '1';  /* 十进制串最高位非零 */
            s[n] = '\0';
            check_str_roundtrip(s);
            free(s);
        }
        /* 5 万位：非 0 开头定长串 */
        const size_t n2 = 50000U;
        char *s2 = (char *)malloc(n2 + 1U);
        CHECK(s2 != NULL);
        if (s2 != NULL) {
            for (size_t i = 0U; i < n2; i++) {
                s2[i] = (char)('1' + (int)(i % 9U));
            }
            s2[n2] = '\0';
            check_str_roundtrip(s2);
            free(s2);
        }
    }
}

/*
 * brief: 已知值精确输出（与 10^k 邻域的边界）
 */
static void test_known_values(void)
{
    bigint_bin_ty v;
    bigint_bin_init(&v);
    char buf[128];

    CHECK(bigint_bin_from_u64(&v, 999999999U) == BIGINT_OK_E);
    CHECK(bigint_bin_to_str(&v, 10, buf, sizeof(buf), NULL) == BIGINT_OK_E);
    CHECK(strcmp(buf, "999999999") == 0);

    CHECK(bigint_bin_from_u64(&v, 1000000000U) == BIGINT_OK_E);
    CHECK(bigint_bin_to_str(&v, 10, buf, sizeof(buf), NULL) == BIGINT_OK_E);
    CHECK(strcmp(buf, "1000000000") == 0);

    CHECK(bigint_bin_from_u64(&v, 0xFFFFFFFFU) == BIGINT_OK_E);
    CHECK(bigint_bin_to_str(&v, 10, buf, sizeof(buf), NULL) == BIGINT_OK_E);
    CHECK(strcmp(buf, "4294967295") == 0);

    /* 10^36 = 4 个 dec 肢边界 */
    CHECK(bigint_bin_from_str(&v, "1000000000000000000000000000000000000",
            10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_to_str(&v, 10, buf, sizeof(buf), NULL) == BIGINT_OK_E);
    CHECK(strcmp(buf, "1000000000000000000000000000000000000") == 0);

    bigint_bin_free(&v);
}

/*
 * brief: 部分消费与错误语义（end 指针、PARSE_E）
 */
static void test_parse_semantics(void)
{
    bigint_bin_ty v;
    bigint_bin_init(&v);

    /* 部分消费：数字后跟垃圾字符 */
    const char *end = NULL;
    CHECK(bigint_bin_from_str(&v, "12345xyz", 10, &end) == BIGINT_OK_E);
    CHECK((end != NULL) && (strcmp(end, "xyz") == 0));
    {
        char buf[64];
        CHECK(bigint_bin_to_str(&v, 10, buf, sizeof(buf), NULL) == BIGINT_OK_E);
        CHECK(strcmp(buf, "12345") == 0);
    }

    /* 全非法：不消费且返回 PARSE_E，end 指向原串 */
    const char *bad = "abc";
    CHECK(bigint_bin_from_str(&v, bad, 10, &end) == BIGINT_ERR_PARSE_E);
    CHECK(end == bad);

    /* 仅符号：PARSE_E */
    CHECK(bigint_bin_from_str(&v, "-", 10, &end) == BIGINT_ERR_PARSE_E);

    /* 空串：PARSE_E */
    CHECK(bigint_bin_from_str(&v, "", 10, &end) == BIGINT_ERR_PARSE_E);

    /* 负零：解析为零（无符号） */
    CHECK(bigint_bin_from_str(&v, "-0", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&v));

    /* 前导零不影响值 */
    CHECK(bigint_bin_from_str(&v, "000000000000000000123", 10, NULL)
            == BIGINT_OK_E);
    {
        char buf[64];
        CHECK(bigint_bin_to_str(&v, 10, buf, sizeof(buf), NULL) == BIGINT_OK_E);
        CHECK(strcmp(buf, "123") == 0);
    }

    /* base 10 不认十六进制前缀 */
    CHECK(bigint_bin_from_str(&v, "0x1F", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&v));

    bigint_bin_free(&v);
}

/*
 * brief: 幂等性：to_str → from_str → to_str 两次输出一致
 */
static void test_double_roundtrip(void)
{
    bigint_bin_ty v;
    bigint_bin_init(&v);
    CHECK(bigint_bin_from_str(&v, "-987654321098765432109876543210987654321"
            "0987654321098765432109876543210", 10, NULL) == BIGINT_OK_E);
    size_t needed = 0U;
    CHECK(bigint_bin_to_str(&v, 10, NULL, 0, &needed) == BIGINT_OK_E);
    char *s1 = (char *)malloc(needed);
    CHECK(s1 != NULL);
    if (s1 != NULL) {
        CHECK(bigint_bin_to_str(&v, 10, s1, needed, &needed) == BIGINT_OK_E);
        bigint_bin_ty v2;
        bigint_bin_init(&v2);
        CHECK(bigint_bin_from_str(&v2, s1, 10, NULL) == BIGINT_OK_E);
        size_t needed2 = 0U;
        CHECK(bigint_bin_to_str(&v2, 10, NULL, 0, &needed2) == BIGINT_OK_E);
        char *s2 = (char *)malloc(needed2);
        CHECK(s2 != NULL);
        if (s2 != NULL) {
            CHECK(bigint_bin_to_str(&v2, 10, s2, needed2, &needed2)
                    == BIGINT_OK_E);
            CHECK(strcmp(s1, s2) == 0);
            free(s2);
        }
        bigint_bin_free(&v2);
        free(s1);
    }
    bigint_bin_free(&v);
}

/*
 * brief: 大小查询（buf == NULL）与溢出语义
 */
static void test_size_query(void)
{
    bigint_bin_ty v;
    bigint_bin_init(&v);
    CHECK(bigint_bin_from_str(&v, "12345678901234567890", 10, NULL)
            == BIGINT_OK_E);
    size_t needed = 0U;
    CHECK(bigint_bin_to_str(&v, 10, NULL, 0, &needed) == BIGINT_OK_E);
    CHECK(needed == 21U);  /* 20 位 + '\0' */
    char small[8];
    CHECK(bigint_bin_to_str(&v, 10, small, sizeof(small), NULL)
            == BIGINT_ERR_OVERFLOW_E);
    bigint_bin_free(&v);
}

int main(void)
{
    test_roundtrip();
    test_known_values();
    test_parse_semantics();
    test_double_roundtrip();
    test_size_query();
    if (g_fail == 0) {
        printf("bigint_str: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
