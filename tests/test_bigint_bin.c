/* 单元测试：bigint_bin 全 API */
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

static bigint_err_ty from_str(bigint_bin_ty *v, const char *s) {
    return bigint_bin_from_str(v, s, 10, NULL);
}

static void expect_str(const char *desc, const bigint_bin_ty *v,
        const char *expect) {
    size_t needed = 0;
    bigint_bin_to_str(v, 10, NULL, 0, &needed);
    char *buf = (char *)malloc(needed);
    if (buf == NULL) { CHECK(0); return; }
    bigint_bin_to_str(v, 10, buf, needed, &needed);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
    free(buf);
}

static void test_lifecycle(void) {
    bigint_bin_ty a;
    CHECK(bigint_bin_init(&a) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&a));
    CHECK(bigint_bin_sign(&a) == BIGINT_SIGN_ZERO_E);
    CHECK(bigint_bin_bit_len(&a) == 0);
    CHECK(bigint_bin_popcount(&a) == 0);

    bigint_bin_ty b;
    CHECK(bigint_bin_init_cap(&b, 8) == BIGINT_OK_E);
    CHECK(bigint_bin_from_u64(&b, 42) == BIGINT_OK_E);
    bigint_bin_ty c;
    CHECK(bigint_bin_init(&c) == BIGINT_OK_E);
    CHECK(bigint_bin_copy(&c, &b) == BIGINT_OK_E);
    expect_str("copy", &c, "42");
    CHECK(bigint_bin_cmp(&b, &c) == 0);

    bigint_bin_move(&c, &b);
    CHECK(bigint_bin_is_zero(&b));
    expect_str("move", &c, "42");

    CHECK(bigint_bin_shrink(&c) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp_abs(&c, &c) == 0);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
}

static void test_convert(void) {
    bigint_bin_ty v;
    bigint_bin_init(&v);

    CHECK(bigint_bin_from_u64(&v, UINT64_MAX) == BIGINT_OK_E);
    uint64_t u = 0;
    CHECK(bigint_bin_to_u64(&v, &u) == BIGINT_OK_E);
    CHECK(u == UINT64_MAX);

    CHECK(bigint_bin_from_i64(&v, INT64_MIN) == BIGINT_OK_E);
    int64_t i = 0;
    CHECK(bigint_bin_to_i64(&v, &i) == BIGINT_OK_E);
    CHECK(i == INT64_MIN);

    CHECK(bigint_bin_from_i64(&v, -5) == BIGINT_OK_E);
    CHECK(bigint_bin_to_u64(&v, &u) == BIGINT_ERR_OVERFLOW_E);

    CHECK(bigint_bin_from_u64(&v, 0) == BIGINT_OK_E);
    CHECK(bigint_bin_to_i64(&v, &i) == BIGINT_OK_E && i == 0);

    /* to_u64 溢出 */
    CHECK(bigint_bin_from_u64(&v, UINT64_MAX) == BIGINT_OK_E);
    bigint_bin_ty big;
    bigint_bin_init(&big);
    CHECK(bigint_bin_from_str(&big, "18446744073709551616", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_to_u64(&big, &u) == BIGINT_ERR_OVERFLOW_E);

    bigint_bin_free(&v);
    bigint_bin_free(&big);
}

static void test_str(void) {
    bigint_bin_ty v;
    bigint_bin_init(&v);

    CHECK(from_str(&v, "0") == BIGINT_OK_E);
    expect_str("zero", &v, "0");

    CHECK(from_str(&v, "-123456789012345678901234567890") == BIGINT_OK_E);
    expect_str("negative", &v, "-123456789012345678901234567890");

    CHECK(from_str(&v, "12345") == BIGINT_OK_E);
    size_t needed = 0;
    CHECK(bigint_bin_to_str(&v, 2, NULL, 0, &needed) == BIGINT_OK_E);
    char *buf = (char *)malloc(needed);
    CHECK(buf != NULL);
    CHECK(bigint_bin_to_str(&v, 2, buf, needed, &needed) == BIGINT_OK_E);
    CHECK(strcmp(buf, "11000000111001") == 0);
    free(buf);

    CHECK(from_str(&v, "12345") == BIGINT_OK_E);
    needed = 0;
    CHECK(bigint_bin_to_str(&v, 16, NULL, 0, &needed) == BIGINT_OK_E);
    buf = (char *)malloc(needed);
    CHECK(buf != NULL);
    CHECK(bigint_bin_to_str(&v, 16, buf, needed, &needed) == BIGINT_OK_E);
    CHECK(strcmp(buf, "3039") == 0);
    free(buf);

    /* 0x 前缀 */
    CHECK(bigint_bin_from_str(&v, "0xdeadbeef", 16, NULL) == BIGINT_OK_E);
    expect_str("0x prefix", &v, "3735928559");

    /* 0b 前缀 */
    CHECK(bigint_bin_from_str(&v, "0b1010", 2, NULL) == BIGINT_OK_E);
    expect_str("0b prefix", &v, "10");

    /* 大小写 */
    CHECK(bigint_bin_from_str(&v, "DeAdBeEf", 16, NULL) == BIGINT_OK_E);
    expect_str("upper hex", &v, "3735928559");

    /* 部分消费：end 指针 */
    {
        const char *tail = NULL;
        CHECK(bigint_bin_from_str(&v, "123abc", 10, &tail) == BIGINT_OK_E);
        expect_str("partial", &v, "123");
        CHECK(tail != NULL && strcmp(tail, "abc") == 0);
    }

    /* 首字符非法 */
    {
        size_t old_len = 1;
        const char *tail = NULL;
        bigint_bin_ty before;
        bigint_bin_init(&before);
        CHECK(from_str(&before, "999") == BIGINT_OK_E);
        CHECK(bigint_bin_from_str(&v, "abc", 10, &tail) == BIGINT_ERR_PARSE_E);
        CHECK(tail != NULL && strcmp(tail, "abc") == 0);
        /* val 应未改变（仍为旧值） */
        (void)old_len;
        expect_str("parse fail leaves val", &v, "123");
        bigint_bin_free(&before);
    }

    /* base 越界 */
    CHECK(bigint_bin_from_str(&v, "10", 1, NULL) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_bin_from_str(&v, "10", 37, NULL) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_bin_to_str(&v, 37, NULL, 0, NULL) == BIGINT_ERR_INVALID_E);

    /* 缓冲区不足 */
    {
        char small[4];
        size_t n = 0;
        CHECK(from_str(&v, "12345") == BIGINT_OK_E);
        CHECK(bigint_bin_to_str(&v, 10, small, sizeof(small), &n)
                == BIGINT_ERR_OVERFLOW_E);
        CHECK(n > sizeof(small));
    }

    bigint_bin_free(&v);
}

static void test_arith(void) {
    bigint_bin_ty a, b, r;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&r);

    /* 加减 */
    CHECK(from_str(&a, "123456789123456789") == BIGINT_OK_E);
    CHECK(from_str(&b, "987654321987654321") == BIGINT_OK_E);
    CHECK(bigint_bin_add(&r, &a, &b) == BIGINT_OK_E);
    expect_str("add", &r, "1111111111111111110");
    CHECK(bigint_bin_sub(&r, &b, &a) == BIGINT_OK_E);
    expect_str("sub", &r, "864197532864197532");

    /* 负数加减 */
    CHECK(from_str(&a, "-100") == BIGINT_OK_E);
    CHECK(from_str(&b, "30") == BIGINT_OK_E);
    CHECK(bigint_bin_add(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg add", &r, "-70");
    CHECK(bigint_bin_sub(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg sub", &r, "-130");

    /* 别名 */
    CHECK(from_str(&a, "100") == BIGINT_OK_E);
    CHECK(from_str(&b, "200") == BIGINT_OK_E);
    CHECK(bigint_bin_add(&a, &a, &b) == BIGINT_OK_E);
    expect_str("alias add", &a, "300");

    /* 乘法 */
    CHECK(from_str(&a, "123456789") == BIGINT_OK_E);
    CHECK(from_str(&b, "987654321") == BIGINT_OK_E);
    CHECK(bigint_bin_mul(&r, &a, &b) == BIGINT_OK_E);
    expect_str("mul", &r, "121932631112635269");

    /* 乘法零与负 */
    CHECK(from_str(&a, "-12") == BIGINT_OK_E);
    CHECK(from_str(&b, "0") == BIGINT_OK_E);
    CHECK(bigint_bin_mul(&r, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&r));
    CHECK(from_str(&b, "-5") == BIGINT_OK_E);
    CHECK(bigint_bin_mul(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg mul", &r, "60");

    /* mul_ex 方法分派 */
    {
        bigint_mul_method_ty m;
        m.algo = BIGINT_MUL_SCHOOLBOOK_E;
        m.params.schoolbook.reserved = 0;
        CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_OK_E);
        expect_str("schoolbook", &r, "60");
        m.algo = BIGINT_MUL_KARATSUBA_E;
        m.params.karatsuba.cutoff = 0;
        CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_OK_E);
        expect_str("karatsuba", &r, "60");
        m.algo = BIGINT_MUL_AUTO_E;
        CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_OK_E);
        expect_str("auto", &r, "60");
        m.algo = BIGINT_MUL_TOOM_COOK_E;
        CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_UNSUPPORTED_E);
        m.algo = (bigint_mul_algo_ty)99;
        CHECK(bigint_bin_mul_ex(&r, &a, &b, &m) == BIGINT_ERR_INVALID_E);
    }

    /* 大数乘法（触发 Karatsuba） */
    {
        bigint_bin_ty x, y;
        bigint_bin_init(&x);
        bigint_bin_init(&y);
        char *s1 = (char *)malloc(101);
        char *s2 = (char *)malloc(101);
        CHECK(s1 && s2);
        memset(s1, '9', 100); s1[100] = '\0';
        memset(s2, '7', 100); s2[100] = '\0';
        CHECK(bigint_bin_from_str(&x, s1, 10, NULL) == BIGINT_OK_E);
        CHECK(bigint_bin_from_str(&y, s2, 10, NULL) == BIGINT_OK_E);
        CHECK(bigint_bin_mul(&r, &x, &y) == BIGINT_OK_E);
        /* (10^100 - 1)(77...7) = 99 个 '7' + '6' + 99 个 '2' + '3' */
        {
            char *expect = (char *)malloc(201);
            CHECK(expect != NULL);
            if (expect != NULL) {
                memset(expect, '7', 99);
                expect[99] = '6';
                memset(expect + 100, '2', 99);
                expect[199] = '3';
                expect[200] = '\0';
                expect_str("big mul", &r, expect);
                free(expect);
            }
        }
        free(s1);
        free(s2);
        bigint_bin_free(&x);
        bigint_bin_free(&y);
    }

    /* 除法 */
    CHECK(from_str(&a, "100") == BIGINT_OK_E);
    CHECK(from_str(&b, "7") == BIGINT_OK_E);
    CHECK(bigint_bin_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("div", &r, "14");
    CHECK(bigint_bin_div_rem(NULL, &r, &a, &b) == BIGINT_OK_E);
    expect_str("rem", &r, "2");

    /* 除零 */
    CHECK(from_str(&b, "0") == BIGINT_OK_E);
    CHECK(bigint_bin_div_rem(&r, &r, &a, &b) == BIGINT_ERR_DIV_ZERO_E);

    /* 负数除法：截断语义 */
    CHECK(from_str(&a, "-100") == BIGINT_OK_E);
    CHECK(from_str(&b, "7") == BIGINT_OK_E);
    CHECK(bigint_bin_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("neg div", &r, "-14");
    CHECK(bigint_bin_div_rem(NULL, &r, &a, &b) == BIGINT_OK_E);
    expect_str("neg rem", &r, "-2");

    CHECK(from_str(&a, "100") == BIGINT_OK_E);
    CHECK(from_str(&b, "-7") == BIGINT_OK_E);
    CHECK(bigint_bin_div_rem(&r, NULL, &a, &b) == BIGINT_OK_E);
    expect_str("div by neg", &r, "-14");

    /* |lhs| < |rhs| */
    CHECK(from_str(&a, "5") == BIGINT_OK_E);
    CHECK(from_str(&b, "10") == BIGINT_OK_E);
    {
        bigint_bin_ty q2, r2;
        bigint_bin_init(&q2);
        bigint_bin_init(&r2);
        CHECK(bigint_bin_div_rem(&q2, &r2, &a, &b) == BIGINT_OK_E);
        expect_str("small div", &q2, "0");
        expect_str("small rem", &r2, "5");
        bigint_bin_free(&q2);
        bigint_bin_free(&r2);
    }

    /* 大数除法（多肢 Algorithm D） */
    {
        bigint_bin_ty x, y, q, rem;
        bigint_bin_init(&x); bigint_bin_init(&y);
        bigint_bin_init(&q); bigint_bin_init(&rem);
        CHECK(from_str(&x, "123456789012345678901234567890") == BIGINT_OK_E);
        CHECK(from_str(&y, "123456789") == BIGINT_OK_E);
        CHECK(bigint_bin_div_rem(&q, &rem, &x, &y) == BIGINT_OK_E);
        expect_str("big div q", &q, "1000000000100000000010");
        expect_str("big div r", &rem, "0");
        /* 验证 x = q*y + rem */
        bigint_bin_ty t;
        bigint_bin_init(&t);
        CHECK(bigint_bin_mul(&t, &q, &y) == BIGINT_OK_E);
        CHECK(bigint_bin_add(&t, &t, &rem) == BIGINT_OK_E);
        CHECK(bigint_bin_cmp(&t, &x) == 0);
        bigint_bin_free(&t);
        bigint_bin_free(&x); bigint_bin_free(&y);
        bigint_bin_free(&q); bigint_bin_free(&rem);
    }

    /* 符号与幅值 */
    CHECK(from_str(&a, "-42") == BIGINT_OK_E);
    CHECK(bigint_bin_abs(&a) == BIGINT_OK_E);
    expect_str("abs", &a, "42");
    CHECK(bigint_bin_neg(&a) == BIGINT_OK_E);
    expect_str("neg", &a, "-42");
    CHECK(bigint_bin_neg(&a) == BIGINT_OK_E);
    CHECK(bigint_bin_from_u64(&a, 0) == BIGINT_OK_E);
    CHECK(bigint_bin_neg(&a) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&a));

    /* 幂 */
    CHECK(from_str(&a, "2") == BIGINT_OK_E);
    CHECK(bigint_bin_pow(&r, &a, 10) == BIGINT_OK_E);
    expect_str("pow", &r, "1024");
    CHECK(bigint_bin_pow(&r, &a, 0) == BIGINT_OK_E);
    expect_str("pow0", &r, "1");
    CHECK(bigint_bin_from_u64(&a, 0) == BIGINT_OK_E);
    CHECK(bigint_bin_pow(&r, &a, 0) == BIGINT_OK_E);
    expect_str("0^0", &r, "1");

    /* 负底数幂 */
    CHECK(from_str(&a, "-2") == BIGINT_OK_E);
    CHECK(bigint_bin_pow(&r, &a, 3) == BIGINT_OK_E);
    expect_str("neg pow odd", &r, "-8");
    CHECK(bigint_bin_pow(&r, &a, 4) == BIGINT_OK_E);
    expect_str("neg pow even", &r, "16");

    /* 模幂 */
    CHECK(from_str(&a, "4") == BIGINT_OK_E);
    CHECK(from_str(&b, "3") == BIGINT_OK_E);
    CHECK(from_str(&r, "5") == BIGINT_OK_E);
    bigint_bin_ty res;
    bigint_bin_init(&res);
    CHECK(bigint_bin_pow_mod(&res, &a, &b, &r) == BIGINT_OK_E);
    expect_str("pow_mod", &res, "4");   /* 4^3 mod 5 = 64 mod 5 = 4 */
    /* mod = 0 */
    CHECK(from_str(&r, "0") == BIGINT_OK_E);
    CHECK(bigint_bin_pow_mod(&res, &a, &b, &r) == BIGINT_ERR_DIV_ZERO_E);
    /* 负 exp / mod */
    CHECK(from_str(&r, "5") == BIGINT_OK_E);
    bigint_bin_ty ne;
    bigint_bin_init(&ne);
    CHECK(from_str(&ne, "-1") == BIGINT_OK_E);
    CHECK(bigint_bin_pow_mod(&res, &a, &ne, &r) == BIGINT_ERR_INVALID_E);
    CHECK(from_str(&ne, "1") == BIGINT_OK_E);
    CHECK(bigint_bin_from_str(&r, "-5", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_pow_mod(&res, &a, &ne, &r) == BIGINT_ERR_INVALID_E);
    bigint_bin_free(&ne);
    bigint_bin_free(&res);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&r);
}

static void test_gcd(void) {
    bigint_bin_ty a, b, r;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&r);

    /* NULL 参数 */
    CHECK(bigint_bin_gcd(NULL, &a, &b) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_bin_gcd(&r, NULL, &b) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_bin_gcd(&r, &a, NULL) == BIGINT_ERR_INVALID_E);

    /* gcd(0, 0) = 0，结果规范化为零 */
    CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
    CHECK(bigint_bin_is_zero(&r));
    CHECK(bigint_bin_sign(&r) == BIGINT_SIGN_ZERO_E);
    expect_str("gcd(0,0)", &r, "0");

    /* gcd(a, 0) = |a|，gcd(0, b) = |b| */
    CHECK(from_str(&a, "-42") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
    expect_str("gcd(-42,0)", &r, "42");
    CHECK(bigint_bin_sign(&r) == BIGINT_SIGN_POS_E);
    CHECK(bigint_bin_gcd(&r, &b, &a) == BIGINT_OK_E);
    expect_str("gcd(0,-42)", &r, "42");

    /* 负数：结果恒非负 */
    CHECK(from_str(&b, "18") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
    expect_str("gcd(-42,18)", &r, "6");
    CHECK(from_str(&b, "-18") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
    expect_str("gcd(-42,-18)", &r, "6");
    CHECK(bigint_bin_sign(&r) == BIGINT_SIGN_POS_E);

    /* 互质 */
    CHECK(from_str(&a, "17") == BIGINT_OK_E);
    CHECK(from_str(&b, "31") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
    expect_str("gcd coprime", &r, "1");

    /* 2 的幂公因子：gcd(2^100, 3*2^64) = 2^64 */
    CHECK(from_str(&a, "1267650600228229401496703205376") == BIGINT_OK_E);
    CHECK(from_str(&b, "55340232221128654848") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
    expect_str("gcd pow2", &r, "18446744073709551616");

    /* 大数：g*x 与 g*y（x、y 互质）约出 g */
    {
        bigint_bin_ty g, x, y;
        bigint_bin_init(&g);
        bigint_bin_init(&x);
        bigint_bin_init(&y);
        CHECK(from_str(&g, "123456789123456789123456789") == BIGINT_OK_E);
        CHECK(from_str(&x, "1000003") == BIGINT_OK_E);
        CHECK(from_str(&y, "999983") == BIGINT_OK_E);
        CHECK(bigint_bin_mul(&a, &g, &x) == BIGINT_OK_E);
        CHECK(bigint_bin_mul(&b, &g, &y) == BIGINT_OK_E);
        /* 破坏符号与 2 因子：乘入 -8，g 本身为奇数，故 gcd 不变 */
        CHECK(bigint_bin_shl(&a, &a, 3) == BIGINT_OK_E);
        CHECK(bigint_bin_neg(&b) == BIGINT_OK_E);
        CHECK(bigint_bin_gcd(&r, &a, &b) == BIGINT_OK_E);
        expect_str("gcd big", &r, "123456789123456789123456789");
        bigint_bin_free(&g);
        bigint_bin_free(&x);
        bigint_bin_free(&y);
    }

    /* dst 与输入别名 */
    CHECK(from_str(&a, "1071") == BIGINT_OK_E);
    CHECK(from_str(&b, "462") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&a, &a, &b) == BIGINT_OK_E);
    expect_str("gcd alias lhs", &a, "21");
    CHECK(from_str(&a, "1071") == BIGINT_OK_E);
    CHECK(bigint_bin_gcd(&b, &a, &b) == BIGINT_OK_E);
    expect_str("gcd alias rhs", &b, "21");
    CHECK(bigint_bin_gcd(&a, &a, &a) == BIGINT_OK_E);
    expect_str("gcd alias both", &a, "1071");

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&r);
}

static void test_bits(void) {
    bigint_bin_ty a, b, r;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&r);

    /* 移位 */
    CHECK(from_str(&a, "5") == BIGINT_OK_E);
    CHECK(bigint_bin_shl(&r, &a, 3) == BIGINT_OK_E);
    expect_str("shl", &r, "40");
    CHECK(bigint_bin_shl(&r, &a, 0) == BIGINT_OK_E);
    expect_str("shl0", &r, "5");

    CHECK(from_str(&a, "-5") == BIGINT_OK_E);
    CHECK(bigint_bin_shr(&r, &a, 1) == BIGINT_OK_E);
    expect_str("shr neg floor", &r, "-3");   /* floor(-5/2) = -3 */
    CHECK(from_str(&a, "17") == BIGINT_OK_E);
    CHECK(bigint_bin_shr(&r, &a, 2) == BIGINT_OK_E);
    expect_str("shr", &r, "4");

    /* 位与 / 或 / 异或（负数补码语义） */
    CHECK(from_str(&a, "12") == BIGINT_OK_E);   /* 1100 */
    CHECK(from_str(&b, "10") == BIGINT_OK_E);   /* 1010 */
    CHECK(bigint_bin_bit_and(&r, &a, &b) == BIGINT_OK_E);
    expect_str("and", &r, "8");
    CHECK(bigint_bin_bit_or(&r, &a, &b) == BIGINT_OK_E);
    expect_str("or", &r, "14");
    CHECK(bigint_bin_bit_xor(&r, &a, &b) == BIGINT_OK_E);
    expect_str("xor", &r, "6");

    /* -1 & x == x */
    CHECK(from_str(&a, "-1") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_and(&r, &a, &b) == BIGINT_OK_E);
    expect_str("-1 and x", &r, "10");
    /* -1 | x == -1 */
    CHECK(bigint_bin_bit_or(&r, &a, &b) == BIGINT_OK_E);
    expect_str("-1 or x", &r, "-1");

    /* -12 & -10 */
    CHECK(from_str(&a, "-12") == BIGINT_OK_E);
    CHECK(from_str(&b, "-10") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_and(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg and", &r, "-12");
    CHECK(bigint_bin_bit_or(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg or", &r, "-10");
    CHECK(bigint_bin_bit_xor(&r, &a, &b) == BIGINT_OK_E);
    expect_str("neg xor", &r, "2");

    /* 位测试 */
    CHECK(from_str(&a, "5") == BIGINT_OK_E);   /* 101 */
    CHECK(bigint_bin_bit_test(&a, 0));
    CHECK(!bigint_bin_bit_test(&a, 1));
    CHECK(bigint_bin_bit_test(&a, 2));
    CHECK(from_str(&a, "-1") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_test(&a, 100));

    /* 位设置 */
    CHECK(from_str(&a, "5") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_set(&a, 3, true) == BIGINT_OK_E);
    expect_str("bit set", &a, "13");
    CHECK(bigint_bin_bit_set(&a, 0, false) == BIGINT_OK_E);
    expect_str("bit clear", &a, "12");

    /* 位长与 popcount */
    CHECK(from_str(&a, "0") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_len(&a) == 0);
    CHECK(from_str(&a, "255") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_len(&a) == 8);
    CHECK(bigint_bin_popcount(&a) == 8);
    CHECK(from_str(&a, "-255") == BIGINT_OK_E);
    CHECK(bigint_bin_bit_len(&a) == 8);
    CHECK(bigint_bin_popcount(&a) == 8);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&r);
}

int main(void) {
    test_lifecycle();
    test_convert();
    test_str();
    test_arith();
    test_gcd();
    test_bits();
    if (g_fail == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", g_fail);
    return 1;
}