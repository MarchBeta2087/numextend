/* 单元测试：bigint_dec 全 API */
#include "nex/bigint/dec/nex_bigint_dec.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
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

static bigint_err_ty from_str(bigint_dec_ty *v, const char *s) {
    return bigint_dec_from_str(v, s, 10, NULL);
}

static void expect_str(const char *desc, const bigint_dec_ty *v,
        const char *expect) {
    size_t needed = 0;
    bigint_dec_to_str(v, 10, NULL, 0, &needed);
    char *buf = (char *)malloc(needed);
    if (buf == NULL) { CHECK(0); return; }
    bigint_dec_to_str(v, 10, buf, needed, &needed);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
    free(buf);
}

static void test_lifecycle(void) {
    bigint_dec_ty a;
    CHECK(bigint_dec_init(&a) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&a));
    CHECK(bigint_dec_sign(&a) == BIGINT_SIGN_ZERO_E);
    CHECK(bigint_dec_digit_len(&a) == 0);

    bigint_dec_ty b;
    CHECK(bigint_dec_init_cap(&b, 8) == BIGINT_OK_E);
    CHECK(bigint_dec_from_u64(&b, 42) == BIGINT_OK_E);
    bigint_dec_ty c;
    CHECK(bigint_dec_init(&c) == BIGINT_OK_E);
    CHECK(bigint_dec_copy(&c, &b) == BIGINT_OK_E);
    expect_str("copy", &c, "42");
    CHECK(bigint_dec_cmp(&b, &c) == 0);

    bigint_dec_move(&c, &b);
    CHECK(bigint_dec_is_zero(&b));
    expect_str("move", &c, "42");

    CHECK(bigint_dec_shrink(&c) == BIGINT_OK_E);
    CHECK(bigint_dec_cmp_abs(&c, &c) == 0);

    bigint_dec_free(&a);
    bigint_dec_free(&b);
    bigint_dec_free(&c);
}

static void test_convert(void) {
    bigint_dec_ty v;
    bigint_dec_init(&v);

    CHECK(bigint_dec_from_u64(&v, UINT64_MAX) == BIGINT_OK_E);
    uint64_t u = 0;
    CHECK(bigint_dec_to_u64(&v, &u) == BIGINT_OK_E);
    CHECK(u == UINT64_MAX);
    expect_str("u64 max", &v, "18446744073709551615");

    CHECK(bigint_dec_from_i64(&v, INT64_MIN) == BIGINT_OK_E);
    int64_t i = 0;
    CHECK(bigint_dec_to_i64(&v, &i) == BIGINT_OK_E);
    CHECK(i == INT64_MIN);

    CHECK(bigint_dec_from_i64(&v, -5) == BIGINT_OK_E);
    CHECK(bigint_dec_to_u64(&v, &u) == BIGINT_ERR_OVERFLOW_E);

    CHECK(bigint_dec_from_u64(&v, 0) == BIGINT_OK_E);
    CHECK(bigint_dec_to_i64(&v, &i) == BIGINT_OK_E && i == 0);

    /* to_u64 溢出 */
    bigint_dec_ty big;
    bigint_dec_init(&big);
    CHECK(from_str(&big, "18446744073709551616") == BIGINT_OK_E);
    CHECK(bigint_dec_to_u64(&big, &u) == BIGINT_ERR_OVERFLOW_E);
    /* to_i64 双向边界：INT64_MAX 可，+1 不可；INT64_MIN 可，-1 不可 */
    CHECK(from_str(&big, "9223372036854775807") == BIGINT_OK_E);
    CHECK(bigint_dec_to_i64(&big, &i) == BIGINT_OK_E && i == INT64_MAX);
    CHECK(from_str(&big, "9223372036854775808") == BIGINT_OK_E);
    CHECK(bigint_dec_to_i64(&big, &i) == BIGINT_ERR_OVERFLOW_E);
    CHECK(from_str(&big, "-9223372036854775808") == BIGINT_OK_E);
    CHECK(bigint_dec_to_i64(&big, &i) == BIGINT_OK_E && i == INT64_MIN);
    CHECK(from_str(&big, "-9223372036854775809") == BIGINT_OK_E);
    CHECK(bigint_dec_to_i64(&big, &i) == BIGINT_ERR_OVERFLOW_E);

    bigint_dec_free(&v);
    bigint_dec_free(&big);
}

static void test_str(void) {
    bigint_dec_ty v;
    bigint_dec_init(&v);

    CHECK(from_str(&v, "0") == BIGINT_OK_E);
    expect_str("zero", &v, "0");

    CHECK(from_str(&v, "-123456789012345678901234567890") == BIGINT_OK_E);
    expect_str("negative", &v, "-123456789012345678901234567890");

    /* 前导零与 "-0" */
    CHECK(from_str(&v, "000123") == BIGINT_OK_E);
    expect_str("leading zeros", &v, "123");
    CHECK(from_str(&v, "-0") == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&v));
    expect_str("minus zero", &v, "0");

    /* 跨肢输出：低位肢前导补零 */
    CHECK(from_str(&v, "1000000000000000001") == BIGINT_OK_E);
    expect_str("inner zeros", &v, "1000000000000000001");

    /* 部分消费：end 指针 */
    {
        const char *tail = NULL;
        CHECK(bigint_dec_from_str(&v, "123abc", 10, &tail) == BIGINT_OK_E);
        expect_str("partial", &v, "123");
        CHECK(tail != NULL && strcmp(tail, "abc") == 0);
    }

    /* 首字符非法：val 不变 */
    {
        const char *tail = NULL;
        CHECK(bigint_dec_from_str(&v, "abc", 10, &tail) == BIGINT_ERR_PARSE_E);
        CHECK(tail != NULL && strcmp(tail, "abc") == 0);
        expect_str("parse fail leaves val", &v, "123");
        /* 孤立的负号同样视为首字符非法 */
        CHECK(bigint_dec_from_str(&v, "-", 10, &tail) == BIGINT_ERR_PARSE_E);
        expect_str("bare minus leaves val", &v, "123");
    }

    /* base 非 10 */
    CHECK(bigint_dec_from_str(&v, "10", 2, NULL) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_dec_from_str(&v, "10", 16, NULL) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_dec_to_str(&v, 16, NULL, 0, NULL) == BIGINT_ERR_INVALID_E);

    /* 缓冲区不足 */
    {
        char small[4];
        size_t n = 0;
        CHECK(from_str(&v, "12345") == BIGINT_OK_E);
        CHECK(bigint_dec_to_str(&v, 10, small, sizeof(small), &n)
                == BIGINT_ERR_OVERFLOW_E);
        CHECK(n == 6);
    }

    bigint_dec_free(&v);
}

static void test_arith(void) {
    bigint_dec_ty a, b, r;
    bigint_dec_init(&a);
    bigint_dec_init(&b);
    bigint_dec_init(&r);

    /* 加减 */
    CHECK(from_str(&a, "123456789123456789") == BIGINT_OK_E);
    CHECK(from_str(&b, "987654321987654321") == BIGINT_OK_E);
    CHECK(bigint_dec_add(&r, &a, &b) == BIGINT_OK_E);
    expect_str("add", &r, "1111111111111111110");
    CHECK(bigint_dec_sub(&r, &b, &a) == BIGINT_OK_E);
    expect_str("sub", &r, "864197532864197532");

    /* 跨肢进位 / 借位：10^9 边界 */
    CHECK(from_str(&a, "999999999") == BIGINT_OK_E);
    CHECK(bigint_dec_from_u64(&b, 1) == BIGINT_OK_E);
    CHECK(bigint_dec_add(&r, &a, &b) == BIGINT_OK_E);
    expect_str("carry at 10^9", &r, "1000000000");
    CHECK(bigint_dec_sub(&r, &r, &b) == BIGINT_OK_E);
    expect_str("borrow at 10^9", &r, "999999999");

    /* 负数加减 */
    CHECK(from_str(&a, "-100") == BIGINT_OK_E);
    CHECK(from_str(&b, "30") == BIGINT_OK_E);
    CHECK(bigint_dec_add(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg add", &r, "-70");
    CHECK(bigint_dec_sub(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg sub", &r, "-130");

    /* 别名 */
    CHECK(from_str(&a, "100") == BIGINT_OK_E);
    CHECK(from_str(&b, "200") == BIGINT_OK_E);
    CHECK(bigint_dec_add(&a, &a, &b) == BIGINT_OK_E);
    expect_str("alias add", &a, "300");

    /* 乘法 */
    CHECK(from_str(&a, "123456789") == BIGINT_OK_E);
    CHECK(from_str(&b, "987654321") == BIGINT_OK_E);
    CHECK(bigint_dec_mul(&r, &a, &b) == BIGINT_OK_E);
    expect_str("mul", &r, "121932631112635269");

    /* 乘法零与负 */
    CHECK(from_str(&a, "-12") == BIGINT_OK_E);
    CHECK(from_str(&b, "0") == BIGINT_OK_E);
    CHECK(bigint_dec_mul(&r, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&r));
    CHECK(from_str(&b, "-5") == BIGINT_OK_E);
    CHECK(bigint_dec_mul(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg mul", &r, "60");

    /* mul_ex 方法分派 */
    {
        bigint_mul_method_ty m;
        m.algo = BIGINT_MUL_SCHOOLBOOK_E;
        m.params.schoolbook.reserved = 0;
        CHECK(bigint_dec_mul_ex(&r, &a, &b, &m) == BIGINT_OK_E);
        expect_str("schoolbook", &r, "60");
        m.algo = BIGINT_MUL_KARATSUBA_E;
        m.params.karatsuba.cutoff = 0;
        CHECK(bigint_dec_mul_ex(&r, &a, &b, &m) == BIGINT_OK_E);
        expect_str("karatsuba", &r, "60");
        m.algo = BIGINT_MUL_AUTO_E;
        CHECK(bigint_dec_mul_ex(&r, &a, &b, &m) == BIGINT_OK_E);
        expect_str("auto", &r, "60");
        m.algo = BIGINT_MUL_TOOM_COOK_E;
        CHECK(bigint_dec_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_UNSUPPORTED_E);
        m.algo = (bigint_mul_algo_ty)99;
        CHECK(bigint_dec_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_INVALID_E);
    }

    /* 大数乘法（触发 Karatsuba：100 位十进制 ≈ 12 肢，
     * 用 400 位确保双操作数 ≥ 32 肢） */
    {
        bigint_dec_ty x, y;
        bigint_dec_init(&x);
        bigint_dec_init(&y);
        char *s1 = (char *)malloc(401);
        char *s2 = (char *)malloc(401);
        CHECK(s1 && s2);
        memset(s1, '9', 400); s1[400] = '\0';
        memset(s2, '7', 400); s2[400] = '\0';
        CHECK(from_str(&x, s1) == BIGINT_OK_E);
        CHECK(from_str(&y, s2) == BIGINT_OK_E);
        CHECK(x.len >= 32 && y.len >= 32);  /* 确认走 Karatsuba 分支 */
        CHECK(bigint_dec_mul(&r, &x, &y) == BIGINT_OK_E);
        /* (10^400 - 1)(77...7) = 399 个 '7' + '6' + 399 个 '2' + '3' */
        {
            char *expect = (char *)malloc(801);
            CHECK(expect != NULL);
            if (expect != NULL) {
                memset(expect, '7', 399);
                expect[399] = '6';
                memset(expect + 400, '2', 399);
                expect[799] = '3';
                expect[800] = '\0';
                expect_str("big mul", &r, expect);
                free(expect);
            }
        }
        /* 与 schoolbook 对拍 */
        {
            bigint_mul_method_ty m;
            m.algo = BIGINT_MUL_SCHOOLBOOK_E;
            m.params.schoolbook.reserved = 0;
            bigint_dec_ty ref;
            bigint_dec_init(&ref);
            CHECK(bigint_dec_mul_ex(&ref, &x, &y, &m) == BIGINT_OK_E);
            CHECK(bigint_dec_cmp(&r, &ref) == 0);
            bigint_dec_free(&ref);
        }
        free(s1);
        free(s2);
        bigint_dec_free(&x);
        bigint_dec_free(&y);
    }

    /* 除法 */
    CHECK(from_str(&a, "100") == BIGINT_OK_E);
    CHECK(from_str(&b, "7") == BIGINT_OK_E);
    CHECK(bigint_dec_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("div", &r, "14");
    CHECK(bigint_dec_div_rem(NULL, &r, &a, &b) == BIGINT_OK_E);
    expect_str("rem", &r, "2");

    /* 除零：输出参数不被修改 */
    CHECK(from_str(&b, "0") == BIGINT_OK_E);
    CHECK(from_str(&r, "77") == BIGINT_OK_E);
    CHECK(bigint_dec_div_rem(&r, &r, &a, &b) == BIGINT_ERR_DIV_ZERO_E);
    expect_str("divzero leaves quot", &r, "77");

    /* 负数除法：截断语义 */
    CHECK(from_str(&a, "-100") == BIGINT_OK_E);
    CHECK(from_str(&b, "7") == BIGINT_OK_E);
    CHECK(bigint_dec_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("neg div", &r, "-14");
    CHECK(bigint_dec_div_rem(NULL, &r, &a, &b) == BIGINT_OK_E);
    expect_str("neg rem", &r, "-2");

    CHECK(from_str(&a, "100") == BIGINT_OK_E);
    CHECK(from_str(&b, "-7") == BIGINT_OK_E);
    CHECK(bigint_dec_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("div by neg", &r, "-14");

    /* |lhs| < |rhs| */
    CHECK(from_str(&a, "5") == BIGINT_OK_E);
    CHECK(from_str(&b, "10") == BIGINT_OK_E);
    {
        bigint_dec_ty q2, r2;
        bigint_dec_init(&q2);
        bigint_dec_init(&r2);
        CHECK(bigint_dec_div_rem(&q2, &r2, &a, &b) == BIGINT_OK_E);
        expect_str("small div", &q2, "0");
        expect_str("small rem", &r2, "5");
        bigint_dec_free(&q2);
        bigint_dec_free(&r2);
    }

    /* 大数除法（多肢 Algorithm D） */
    {
        bigint_dec_ty x, y, q, rem;
        bigint_dec_init(&x); bigint_dec_init(&y);
        bigint_dec_init(&q); bigint_dec_init(&rem);
        CHECK(from_str(&x, "123456789012345678901234567890") == BIGINT_OK_E);
        CHECK(from_str(&y, "123456789") == BIGINT_OK_E);
        CHECK(bigint_dec_div_rem(&q, &rem, &x, &y) == BIGINT_OK_E);
        expect_str("big div q", &q, "1000000000100000000010");
        expect_str("big div r", &rem, "0");
        /* 验证 x = q*y + rem */
        bigint_dec_ty t;
        bigint_dec_init(&t);
        CHECK(bigint_dec_mul(&t, &q, &y) == BIGINT_OK_E);
        CHECK(bigint_dec_add(&t, &t, &rem) == BIGINT_OK_E);
        CHECK(bigint_dec_cmp(&t, &x) == 0);
        bigint_dec_free(&t);
        bigint_dec_free(&x); bigint_dec_free(&y);
        bigint_dec_free(&q); bigint_dec_free(&rem);
    }

    /* 符号与幅值 */
    CHECK(from_str(&a, "-42") == BIGINT_OK_E);
    CHECK(bigint_dec_abs(&a) == BIGINT_OK_E);
    expect_str("abs", &a, "42");
    CHECK(bigint_dec_neg(&a) == BIGINT_OK_E);
    expect_str("neg", &a, "-42");
    CHECK(bigint_dec_neg(&a) == BIGINT_OK_E);
    CHECK(bigint_dec_from_u64(&a, 0) == BIGINT_OK_E);
    CHECK(bigint_dec_neg(&a) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&a));

    /* 幂 */
    CHECK(from_str(&a, "2") == BIGINT_OK_E);
    CHECK(bigint_dec_pow(&r, &a, 10) == BIGINT_OK_E);
    expect_str("pow", &r, "1024");
    CHECK(bigint_dec_pow(&r, &a, 0) == BIGINT_OK_E);
    expect_str("pow0", &r, "1");
    CHECK(bigint_dec_from_u64(&a, 0) == BIGINT_OK_E);
    CHECK(bigint_dec_pow(&r, &a, 0) == BIGINT_OK_E);
    expect_str("0^0", &r, "1");

    /* 负底数幂 */
    CHECK(from_str(&a, "-2") == BIGINT_OK_E);
    CHECK(bigint_dec_pow(&r, &a, 3) == BIGINT_OK_E);
    expect_str("neg pow odd", &r, "-8");
    CHECK(bigint_dec_pow(&r, &a, 4) == BIGINT_OK_E);
    expect_str("neg pow even", &r, "16");

    /* 模幂 */
    CHECK(from_str(&a, "4") == BIGINT_OK_E);
    CHECK(from_str(&b, "3") == BIGINT_OK_E);
    CHECK(from_str(&r, "5") == BIGINT_OK_E);
    bigint_dec_ty res;
    bigint_dec_init(&res);
    CHECK(bigint_dec_pow_mod(&res, &a, &b, &r) == BIGINT_OK_E);
    expect_str("pow_mod", &res, "4");   /* 4^3 mod 5 = 64 mod 5 = 4 */
    /* 大指数模幂（多肢指数折半路径） */
    CHECK(from_str(&a, "12345678901234567890") == BIGINT_OK_E);
    CHECK(from_str(&b, "987654321987654321") == BIGINT_OK_E);
    CHECK(from_str(&r, "1000000007") == BIGINT_OK_E);
    CHECK(bigint_dec_pow_mod(&res, &a, &b, &r) == BIGINT_OK_E);
    expect_str("big pow_mod", &res, "312017967");
    /* mod = 0 */
    CHECK(from_str(&r, "0") == BIGINT_OK_E);
    CHECK(bigint_dec_pow_mod(&res, &a, &b, &r) == BIGINT_ERR_DIV_ZERO_E);
    /* 负 exp / mod */
    CHECK(from_str(&r, "5") == BIGINT_OK_E);
    bigint_dec_ty ne;
    bigint_dec_init(&ne);
    CHECK(from_str(&ne, "-1") == BIGINT_OK_E);
    CHECK(bigint_dec_pow_mod(&res, &a, &ne, &r) == BIGINT_ERR_INVALID_E);
    CHECK(from_str(&ne, "1") == BIGINT_OK_E);
    CHECK(from_str(&r, "-5") == BIGINT_OK_E);
    CHECK(bigint_dec_pow_mod(&res, &a, &ne, &r) == BIGINT_ERR_INVALID_E);
    bigint_dec_free(&ne);
    bigint_dec_free(&res);

    bigint_dec_free(&a);
    bigint_dec_free(&b);
    bigint_dec_free(&r);
}

static void test_pow10(void) {
    bigint_dec_ty v;
    bigint_dec_init(&v);

    /* mul_pow10：整肢移动 */
    CHECK(bigint_dec_from_u64(&v, 123) == BIGINT_OK_E);
    CHECK(bigint_dec_mul_pow10(&v, 9) == BIGINT_OK_E);
    expect_str("mul_pow10 9", &v, "123000000000");
    /* 余数段：跨肢 */
    CHECK(bigint_dec_mul_pow10(&v, 7) == BIGINT_OK_E);
    expect_str("mul_pow10 16", &v, "1230000000000000000");
    /* 混合：整肢 + 余数 */
    CHECK(bigint_dec_from_u64(&v, 7) == BIGINT_OK_E);
    CHECK(bigint_dec_mul_pow10(&v, 25) == BIGINT_OK_E);
    expect_str("mul_pow10 25", &v, "70000000000000000000000000");
    /* digits == 0 与零值 */
    CHECK(bigint_dec_mul_pow10(&v, 0) == BIGINT_OK_E);
    expect_str("mul_pow10 0", &v, "70000000000000000000000000");
    CHECK(bigint_dec_from_u64(&v, 0) == BIGINT_OK_E);
    CHECK(bigint_dec_mul_pow10(&v, 100) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&v));
    /* 负数保持符号 */
    CHECK(from_str(&v, "-5") == BIGINT_OK_E);
    CHECK(bigint_dec_mul_pow10(&v, 10) == BIGINT_OK_E);
    expect_str("neg mul_pow10", &v, "-50000000000");

    /* div_pow10：截断，含跨肢 */
    CHECK(from_str(&v, "1234567890123456789") == BIGINT_OK_E);
    CHECK(bigint_dec_div_pow10(&v, 9) == BIGINT_OK_E);
    expect_str("div_pow10 9", &v, "1234567890");
    CHECK(bigint_dec_div_pow10(&v, 5) == BIGINT_OK_E);
    expect_str("div_pow10 14", &v, "12345");
    /* 全部除尽 → 零（符号规范化） */
    CHECK(bigint_dec_div_pow10(&v, 100) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&v));
    /* 负数截断向零 */
    CHECK(from_str(&v, "-123456789") == BIGINT_OK_E);
    CHECK(bigint_dec_div_pow10(&v, 8) == BIGINT_OK_E);
    expect_str("neg div_pow10", &v, "-1");
    CHECK(from_str(&v, "-999999999") == BIGINT_OK_E);
    CHECK(bigint_dec_div_pow10(&v, 9) == BIGINT_OK_E);
    CHECK(bigint_dec_is_zero(&v));
    /* digits == 0 无操作 */
    CHECK(from_str(&v, "42") == BIGINT_OK_E);
    CHECK(bigint_dec_div_pow10(&v, 0) == BIGINT_OK_E);
    expect_str("div_pow10 0", &v, "42");

    bigint_dec_free(&v);
}

static void test_digit_len(void) {
    bigint_dec_ty v;
    bigint_dec_init(&v);

    CHECK(bigint_dec_digit_len(&v) == 0);  /* 零 */
    CHECK(bigint_dec_from_u64(&v, 7) == BIGINT_OK_E);
    CHECK(bigint_dec_digit_len(&v) == 1);
    /* 10^9 - 1 与 10^9 的肢边界 */
    CHECK(from_str(&v, "999999999") == BIGINT_OK_E);
    CHECK(v.len == 1);
    CHECK(bigint_dec_digit_len(&v) == 9);
    CHECK(from_str(&v, "1000000000") == BIGINT_OK_E);
    CHECK(v.len == 2);
    CHECK(bigint_dec_digit_len(&v) == 10);
    /* 高位肢恰为 1 的长数 */
    CHECK(from_str(&v, "1000000000000000000000000000") == BIGINT_OK_E);
    CHECK(bigint_dec_digit_len(&v) == 28);
    /* 负数按幅值计 */
    CHECK(from_str(&v, "-999999999999999999") == BIGINT_OK_E);
    CHECK(bigint_dec_digit_len(&v) == 18);

    bigint_dec_free(&v);
}

static void test_limb_boundary(void) {
    bigint_dec_ty a, b, r;
    bigint_dec_init(&a);
    bigint_dec_init(&b);
    bigint_dec_init(&r);

    /* (10^9 - 1) × (10^9 - 1) = 10^18 - 2·10^9 + 1 */
    CHECK(from_str(&a, "999999999") == BIGINT_OK_E);
    CHECK(bigint_dec_mul(&r, &a, &a) == BIGINT_OK_E);
    expect_str("(10^9-1)^2", &r, "999999998000000001");

    /* 10^9 × 10^9 跨两肢 */
    CHECK(from_str(&a, "1000000000") == BIGINT_OK_E);
    CHECK(bigint_dec_mul(&r, &a, &a) == BIGINT_OK_E);
    expect_str("(10^9)^2", &r, "1000000000000000000");

    /* 10^9 - 1 与 10^9 相邻整数的除法 */
    CHECK(from_str(&a, "999999999999999999") == BIGINT_OK_E);  /* 10^18 - 1 */
    CHECK(from_str(&b, "999999999") == BIGINT_OK_E);
    CHECK(bigint_dec_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("(10^18-1)/(10^9-1)", &r, "1000000001");
    CHECK(bigint_dec_div_rem(NULL, &r, &a, &b) == BIGINT_OK_E);
    expect_str("rem", &r, "0");

    /* 全 9 肢加法连锁进位 */
    CHECK(from_str(&a, "999999999999999999999999999") == BIGINT_OK_E);
    CHECK(bigint_dec_from_u64(&b, 1) == BIGINT_OK_E);
    CHECK(bigint_dec_add(&r, &a, &b) == BIGINT_OK_E);
    expect_str("all-nine carry", &r, "1000000000000000000000000000");
    CHECK(bigint_dec_digit_len(&r) == 28);

    bigint_dec_free(&a);
    bigint_dec_free(&b);
    bigint_dec_free(&r);
}

int main(void) {
#ifdef _MSC_VER
    /* CRT 调试堆：逐次分配完整性检查 + 退出时泄漏报告 */
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_CRT_DF
            | _CRTDBG_LEAK_CHECK_DF);
#endif

    test_lifecycle();
    test_convert();
    test_str();
    test_arith();
    test_pow10();
    test_digit_len();
    test_limb_boundary();
    if (g_fail == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", g_fail);
    return 1;
}
