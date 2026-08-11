/* 单元测试：bigcomplex_decimal 全 API（与 float 版平行的十进制复数）。 */
#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"
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

static const bigdecimal_ctx_ty *g_ctx;

static void expect_str(const char *desc, const bigcomplex_decimal_ty *z,
        const char *expect) {
    char buf[512];
    if (bigcomplex_decimal_to_str(z, BIGDECIMAL_FMT_FIXED_E, buf,
            sizeof(buf), NULL) != BIGCOMPLEX_DECIMAL_OK_E) {
        printf("FAIL %s: to_str error, expect %s\n", desc, expect);
        g_fail++;
        return;
    }
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static int parse(bigcomplex_decimal_ty *z, const char *s) {
    return bigcomplex_decimal_from_str(z, s, g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_OK_E ? 0 : -1;
}

static void expect_re(const char *desc, const bigdecimal_ty *v,
        const char *expect) {
    char buf[128];
    bigdecimal_to_str(v, BIGDECIMAL_FMT_FIXED_E, buf, sizeof(buf), NULL);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s re: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static void test_lifecycle(void) {
    bigcomplex_decimal_ty z;
    CHECK(bigcomplex_decimal_init(NULL) == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);
    CHECK(bigcomplex_decimal_init(&z) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(bigdecimal_is_zero(&z.re) && bigdecimal_is_zero(&z.im));

    bigcomplex_decimal_ty src;
    CHECK(bigcomplex_decimal_init(&src) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(parse(&src, "3.5-2.5i") == 0);
    CHECK(bigcomplex_decimal_copy(&z, &src) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(bigcomplex_decimal_eq(&z, &src));
    CHECK(bigcomplex_decimal_copy(NULL, &src)
            == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);

    bigdecimal_ty re;
    bigdecimal_ty im;
    bigdecimal_init(&re);
    bigdecimal_init(&im);
    bigdecimal_from_str(&re, "1.5", g_ctx, NULL);
    bigdecimal_from_str(&im, "-2.5", g_ctx, NULL);
    CHECK(bigcomplex_decimal_from_parts(&z, &re, &im)
            == BIGCOMPLEX_DECIMAL_OK_E);
    expect_str("from_parts", &z, "1.5-2.5i");
    CHECK(bigcomplex_decimal_from_parts(&z, NULL, &im)
            == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);
    bigdecimal_free(&im);
    bigdecimal_free(&re);
    bigcomplex_decimal_free(&src);
    bigcomplex_decimal_free(&z);
    bigcomplex_decimal_free(NULL);
}

static void test_parse_str(void) {
    bigcomplex_decimal_ty z;
    bigcomplex_decimal_init(&z);

    CHECK(parse(&z, "3.5+2.5i") == 0);
    expect_re("3.5+2.5i", &z.re, "3.5");
    expect_re("3.5+2.5i", &z.im, "2.5");
    CHECK(parse(&z, "3.5-2.5i") == 0);
    expect_re("3.5-2.5i", &z.im, "-2.5");
    CHECK(parse(&z, "3.5") == 0);
    expect_re("3.5", &z.im, "0");
    CHECK(parse(&z, "-3.5") == 0);
    expect_re("-3.5", &z.re, "-3.5");
    CHECK(parse(&z, "2.5i") == 0);
    expect_re("2.5i", &z.re, "0");
    expect_re("2.5i", &z.im, "2.5");
    CHECK(parse(&z, "-2.5i") == 0);
    expect_re("-2.5i", &z.im, "-2.5");
    /* 科学计数与特殊值 */
    CHECK(parse(&z, "1e10-2e-5i") == 0);
    expect_re("1e10-2e-5i", &z.re, "10000000000");
    expect_re("1e10-2e-5i", &z.im, "-0.00002");
    CHECK(parse(&z, "inf+infi") == 0);
    CHECK(bigdecimal_is_inf(&z.re) && bigdecimal_is_inf(&z.im));
    CHECK(parse(&z, "nan") == 0);
    CHECK(bigdecimal_is_nan(&z.re) && bigdecimal_is_zero(&z.im));

    /* 部分消费 */
    const char *end = NULL;
    CHECK(bigcomplex_decimal_from_str(&z, "1+2iabc", g_ctx, &end)
            == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'a');
    end = NULL;
    CHECK(bigcomplex_decimal_from_str(&z, "1.5xyz", g_ctx, &end)
            == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'x');

    /* 错误路径 */
    bigcomplex_decimal_from_str(&z, "1+2i", g_ctx, NULL);
    const bigdecimal_flag_ty before = z.re.flag;
    CHECK(bigcomplex_decimal_from_str(&z, "xyz", g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_PARSE_E);
    CHECK(z.re.flag == before);
    CHECK(bigcomplex_decimal_from_str(&z, "1+2", g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_PARSE_E);
    CHECK(bigcomplex_decimal_from_str(&z, "1+", g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_PARSE_E);
    CHECK(bigcomplex_decimal_from_str(&z, "", g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_PARSE_E);
    CHECK(bigcomplex_decimal_from_str(NULL, "1+2i", g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);
    CHECK(bigcomplex_decimal_from_str(&z, "1+2i", NULL, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);

    bigcomplex_decimal_free(&z);
}

static void test_to_str(void) {
    bigcomplex_decimal_ty z;
    bigcomplex_decimal_init(&z);

    CHECK(parse(&z, "1.5+2.5i") == 0);
    expect_str("1.5+2.5i", &z, "1.5+2.5i");
    CHECK(parse(&z, "1.5-2.5i") == 0);
    expect_str("1.5-2.5i", &z, "1.5-2.5i");
    CHECK(parse(&z, "1.5") == 0);
    expect_str("1.5", &z, "1.5");
    CHECK(parse(&z, "2.5i") == 0);
    expect_str("2.5i", &z, "2.5i");
    CHECK(parse(&z, "0") == 0);
    expect_str("0", &z, "0");
    CHECK(parse(&z, "inf+infi") == 0);
    expect_str("inf+infi", &z, "inf+infi");
    CHECK(parse(&z, "1-infi") == 0);
    expect_str("1-infi", &z, "1-infi");

    /* 科学计数格式透传 */
    char buf[512];
    CHECK(parse(&z, "12300+0.00123i") == 0);
    CHECK(bigcomplex_decimal_to_str(&z, BIGDECIMAL_FMT_SCIENTIFIC_E, buf,
            sizeof(buf), NULL) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(strcmp(buf, "1.23e+4+1.23e-3i") == 0);

    /* 往返 */
    const char *strs[] = { "3.5+2.5i", "-1e10+0.5i", "1e-5-2e5i",
            "123456789.123456789i", "1", "-0.5", "inf", "nan+nani" };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        CHECK(parse(&z, strs[i]) == 0);
        char out[600];
        CHECK(bigcomplex_decimal_to_str(&z, BIGDECIMAL_FMT_FIXED_E, out,
                sizeof(out), NULL) == BIGCOMPLEX_DECIMAL_OK_E);
        bigcomplex_decimal_ty back;
        bigcomplex_decimal_init(&back);
        CHECK(bigcomplex_decimal_from_str(&back, out, g_ctx, NULL)
                == BIGCOMPLEX_DECIMAL_OK_E);
        if (bigdecimal_is_nan(&z.re) || bigdecimal_is_nan(&z.im)) {
            CHECK(bigdecimal_is_nan(&back.re) && bigdecimal_is_nan(&back.im));
        } else {
            CHECK(bigcomplex_decimal_eq(&back, &z));
        }
        bigcomplex_decimal_free(&back);
    }

    /* 缓冲不足 / 仅查询 */
    CHECK(parse(&z, "1+2i") == 0);
    size_t need = 0;
    CHECK(bigcomplex_decimal_to_str(&z, BIGDECIMAL_FMT_FIXED_E, NULL, 0,
            &need) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(need == 5U);
    char small[4];
    CHECK(bigcomplex_decimal_to_str(&z, BIGDECIMAL_FMT_FIXED_E, small,
            sizeof(small), &need) == BIGCOMPLEX_DECIMAL_ERR_OVERFLOW_E);
    CHECK(bigcomplex_decimal_to_str(NULL, BIGDECIMAL_FMT_FIXED_E, NULL, 0,
            NULL) == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);

    bigcomplex_decimal_free(&z);
}

static void test_arith(void) {
    bigcomplex_decimal_ty a;
    bigcomplex_decimal_ty b;
    bigcomplex_decimal_ty c;
    bigcomplex_decimal_init(&a);
    bigcomplex_decimal_init(&b);
    bigcomplex_decimal_init(&c);

    /* 十进制精确语义 */
    CHECK(parse(&a, "0.1+0.2i") == 0);
    CHECK(parse(&b, "0.3+0.4i") == 0);
    bigcomplex_decimal_add(&c, &a, &b, g_ctx);
    expect_str("add", &c, "0.4+0.6i");
    bigcomplex_decimal_sub(&c, &a, &b, g_ctx);
    expect_str("sub", &c, "-0.2-0.2i");
    bigcomplex_decimal_mul(&c, &a, &b, g_ctx);
    /* (0.1+0.2i)(0.3+0.4i) = 0.03+0.08i − 0.08... ac−bd = 0.03−0.08 = −0.05
     * bc+ad = 0.06+0.04 = 0.10 → −0.05+0.1i */
    expect_str("mul", &c, "-0.05+0.1i");
    bigcomplex_decimal_div(&c, &a, &a, g_ctx);
    expect_str("div self", &c, "1");

    /* 整数分量 */
    CHECK(parse(&a, "1+2i") == 0);
    CHECK(parse(&b, "3+4i") == 0);
    bigcomplex_decimal_mul(&c, &a, &b, g_ctx);
    expect_str("mul int", &c, "-5+10i");
    bigcomplex_decimal_div(&c, &a, &b, g_ctx);
    /* (1+2i)/(3+4i) = 11/25 + 2/25i = 0.44+0.08i */
    expect_str("div int", &c, "0.44+0.08i");
    bigcomplex_decimal_mul(&c, &c, &b, g_ctx);
    CHECK(bigcomplex_decimal_eq(&c, &a));  /* 除法逆元 */

    /* 纯实×纯虚 */
    CHECK(parse(&a, "2+0i") == 0);
    CHECK(parse(&b, "0+3i") == 0);
    bigcomplex_decimal_mul(&c, &a, &b, g_ctx);
    expect_str("mul 纯实×纯虚", &c, "6i");

    /* NULL / 别名 */
    CHECK(bigcomplex_decimal_add(NULL, &a, &b, g_ctx)
            == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);
    CHECK(bigcomplex_decimal_add(&c, &a, &b, NULL)
            == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);
    CHECK(bigcomplex_decimal_add(&c, &c, &c, g_ctx)
            == BIGCOMPLEX_DECIMAL_OK_E);

    bigcomplex_decimal_free(&c);
    bigcomplex_decimal_free(&b);
    bigcomplex_decimal_free(&a);
}

static void test_abs_conj_arg(void) {
    bigcomplex_decimal_ty z;
    bigdecimal_ty v;
    bigcomplex_decimal_init(&z);
    bigdecimal_init(&v);

    CHECK(parse(&z, "3+4i") == 0);
    CHECK(bigcomplex_decimal_abs(&v, &z, g_ctx) == BIGCOMPLEX_DECIMAL_OK_E);
    expect_re("|3+4i|", &v, "5");
    CHECK(parse(&z, "0") == 0);
    bigcomplex_decimal_abs(&v, &z, g_ctx);
    expect_re("|0|", &v, "0");
    CHECK(parse(&z, "inf") == 0);
    bigcomplex_decimal_abs(&v, &z, g_ctx);
    CHECK(bigdecimal_is_inf(&v));
    CHECK(parse(&z, "nan+1i") == 0);
    bigcomplex_decimal_abs(&v, &z, g_ctx);
    CHECK(bigdecimal_is_nan(&v));
    CHECK(parse(&z, "12+5i") == 0);
    bigcomplex_decimal_abs(&v, &z, g_ctx);
    expect_re("|12+5i|", &v, "13");
    /* 十进制精确：|0.3+0.4i| = 0.5 */
    CHECK(parse(&z, "0.3+0.4i") == 0);
    bigcomplex_decimal_abs(&v, &z, g_ctx);
    expect_re("|0.3+0.4i|", &v, "0.5");

    CHECK(parse(&z, "1+2i") == 0);
    CHECK(bigcomplex_decimal_conj(&z) == BIGCOMPLEX_DECIMAL_OK_E);
    expect_str("conj", &z, "1-2i");
    CHECK(parse(&z, "nan+1i") == 0);
    bigcomplex_decimal_conj(&z);
    expect_str("conj nan", &z, "nan-1i");
    CHECK(bigcomplex_decimal_conj(NULL) == BIGCOMPLEX_DECIMAL_ERR_INVALID_E);

    CHECK(parse(&z, "1+1i") == 0);
    CHECK(bigcomplex_decimal_arg(&v, &z, g_ctx)
            == BIGCOMPLEX_DECIMAL_ERR_UNSUPPORTED_E);

    bigdecimal_free(&v);
    bigcomplex_decimal_free(&z);
}

static void test_special_eq(void) {
    bigcomplex_decimal_ty a;
    bigcomplex_decimal_ty b;
    bigcomplex_decimal_init(&a);
    bigcomplex_decimal_init(&b);

    CHECK(parse(&a, "nan+1i") == 0);
    CHECK(parse(&b, "1+1i") == 0);
    bigcomplex_decimal_add(&a, &a, &b, g_ctx);
    CHECK(bigdecimal_is_nan(&a.re) && bigdecimal_is_nan(&a.im));
    CHECK(parse(&a, "inf+0i") == 0);
    CHECK(parse(&b, "-inf+0i") == 0);
    bigcomplex_decimal_add(&a, &a, &b, g_ctx);
    CHECK(bigdecimal_is_nan(&a.re));
    CHECK(parse(&a, "0+0i") == 0);
    CHECK(parse(&b, "inf+infi") == 0);
    bigcomplex_decimal_mul(&a, &a, &b, g_ctx);
    CHECK(bigdecimal_is_nan(&a.re) && bigdecimal_is_nan(&a.im));

    CHECK(parse(&a, "1+2i") == 0);
    CHECK(parse(&b, "1+2i") == 0);
    CHECK(bigcomplex_decimal_eq(&a, &b));
    CHECK(parse(&b, "1+3i") == 0);
    CHECK(!bigcomplex_decimal_eq(&a, &b));
    CHECK(parse(&a, "nan+0i") == 0);
    CHECK(parse(&b, "nan+0i") == 0);
    CHECK(!bigcomplex_decimal_eq(&a, &b));
    CHECK(parse(&a, "-0+0i") == 0);
    CHECK(parse(&b, "0+0i") == 0);
    CHECK(bigcomplex_decimal_eq(&a, &b));
    CHECK(!bigcomplex_decimal_eq(NULL, &b));

    bigcomplex_decimal_free(&b);
    bigcomplex_decimal_free(&a);
}

int main(void) {
#ifdef _MSC_VER
    /* CRT 调试堆：逐次分配完整性检查 + 退出时泄漏报告 */
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_CRT_DF
            | _CRTDBG_LEAK_CHECK_DF);
#endif

    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    g_ctx = &ctx;

    test_lifecycle();
    test_parse_str();
    test_to_str();
    test_arith();
    test_abs_conj_arg();
    test_special_eq();

    if (g_fail == 0) {
        printf("test_bigcomplex_decimal: ALL PASS\n");
        return 0;
    }
    printf("test_bigcomplex_decimal: %d FAILURES\n", g_fail);
    return 1;
}
