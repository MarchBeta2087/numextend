/* 单元测试：bigcomplex_float 全 API（构造/解析/输出、四则、共轭、模、
 * 相等性、特殊值、错误路径）。 */
#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
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

static const bigfloat_ctx_ty *g_ctx;

static void expect_str(const char *desc, const bigcomplex_float_ty *z,
        const char *expect) {
    char buf[512];
    if (bigcomplex_float_to_str(z, 0, buf, sizeof(buf), NULL)
            != BIGCOMPLEX_FLOAT_OK_E) {
        printf("FAIL %s: to_str error, expect %s\n", desc, expect);
        g_fail++;
        return;
    }
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static int parse(bigcomplex_float_ty *z, const char *s) {
    return bigcomplex_float_from_str(z, s, g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_OK_E ? 0 : -1;
}

/* 分量输出辅助 */
static void expect_re(const char *desc, const bigfloat_ty *v,
        const char *expect) {
    char buf[128];
    bigfloat_to_str(v, 0, buf, sizeof(buf), NULL);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s re: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static void test_lifecycle(void) {
    bigcomplex_float_ty z;
    CHECK(bigcomplex_float_init(NULL) == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_init(&z) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(bigfloat_is_zero(&z.re) && bigfloat_is_zero(&z.im));

    bigcomplex_float_ty src;
    CHECK(bigcomplex_float_init(&src) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(parse(&src, "3.5-2.5i") == 0);
    CHECK(bigcomplex_float_copy(&z, &src) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(bigcomplex_float_eq(&z, &src));
    CHECK(bigcomplex_float_copy(NULL, &src) == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_copy(&z, NULL) == BIGCOMPLEX_FLOAT_ERR_INVALID_E);

    /* from_parts */
    bigfloat_ty re;
    bigfloat_ty im;
    bigfloat_init(&re);
    bigfloat_init(&im);
    bigfloat_from_str(&re, "1.5", g_ctx, NULL);
    bigfloat_from_str(&im, "-2.5", g_ctx, NULL);
    CHECK(bigcomplex_float_from_parts(&z, &re, &im) == BIGCOMPLEX_FLOAT_OK_E);
    expect_str("from_parts", &z, "1.5-2.5i");
    CHECK(bigcomplex_float_from_parts(NULL, &re, &im)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_from_parts(&z, NULL, &im)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    bigfloat_free(&im);
    bigfloat_free(&re);
    bigcomplex_float_free(&src);
    bigcomplex_float_free(&z);
    bigcomplex_float_free(NULL);
}

static void test_parse_str(void) {
    bigcomplex_float_ty z;
    bigcomplex_float_init(&z);

    /* 各种形式 */
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
    expect_re("1e10-2e-5i", &z.re, "10000000000");  /* bigfloat_to_str 定点 */
    expect_re("1e10-2e-5i", &z.im, "-0.00002");
    CHECK(parse(&z, "inf+infi") == 0);
    CHECK(bigfloat_is_inf(&z.re) && bigfloat_is_inf(&z.im));
    CHECK(parse(&z, "nan") == 0);
    CHECK(bigfloat_is_nan(&z.re) && bigfloat_is_zero(&z.im));

    /* 部分消费 */
    const char *end = NULL;
    CHECK(bigcomplex_float_from_str(&z, "1+2iabc", g_ctx, &end)
            == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'a');
    end = NULL;
    CHECK(bigcomplex_float_from_str(&z, "1.5xyz", g_ctx, &end)
            == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'x');

    /* 错误路径 */
    bigcomplex_float_from_str(&z, "1+2i", g_ctx, NULL);
    const bigfloat_flag_ty before = z.re.flag;
    CHECK(bigcomplex_float_from_str(&z, "xyz", g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_ERR_PARSE_E);
    CHECK(z.re.flag == before);  /* dst 不变 */
    CHECK(bigcomplex_float_from_str(&z, "1+2", g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_ERR_PARSE_E);   /* 虚部缺 'i' */
    CHECK(bigcomplex_float_from_str(&z, "1+", g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_ERR_PARSE_E);
    CHECK(bigcomplex_float_from_str(&z, "1+i", g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_ERR_PARSE_E);   /* "i" 无数字 */
    CHECK(bigcomplex_float_from_str(&z, "", g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_ERR_PARSE_E);
    CHECK(bigcomplex_float_from_str(NULL, "1+2i", g_ctx, NULL)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_from_str(&z, "1+2i", NULL, NULL)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);

    bigcomplex_float_free(&z);
}

static void test_to_str(void) {
    bigcomplex_float_ty z;
    bigcomplex_float_init(&z);

    /* 已知输出 */
    CHECK(parse(&z, "1.5+2.5i") == 0);
    expect_str("1.5+2.5i", &z, "1.5+2.5i");
    CHECK(parse(&z, "1.5-2.5i") == 0);
    expect_str("1.5-2.5i", &z, "1.5-2.5i");
    CHECK(parse(&z, "1.5") == 0);
    expect_str("1.5", &z, "1.5");
    CHECK(parse(&z, "2.5i") == 0);
    expect_str("2.5i", &z, "2.5i");
    CHECK(parse(&z, "-2.5i") == 0);
    expect_str("-2.5i", &z, "-2.5i");
    CHECK(parse(&z, "0") == 0);
    expect_str("0", &z, "0");
    CHECK(parse(&z, "inf+infi") == 0);
    expect_str("inf+infi", &z, "inf+infi");
    CHECK(parse(&z, "1-infi") == 0);
    expect_str("1-infi", &z, "1-infi");
    CHECK(parse(&z, "nan+1i") == 0);
    expect_str("nan+1i", &z, "nan+1i");

    /* 往返：to_str → from_str → eq（NaN 分量按 is_nan 特判） */
    const char *strs[] = { "3.5+2.5i", "-1e10+0.5i", "1e-5-2e5i",
            "123456789.123456789i", "1", "-0.5", "inf", "nan+nani" };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        CHECK(parse(&z, strs[i]) == 0);
        char buf[600];
        CHECK(bigcomplex_float_to_str(&z, 0, buf, sizeof(buf), NULL)
                == BIGCOMPLEX_FLOAT_OK_E);
        bigcomplex_float_ty back;
        bigcomplex_float_init(&back);
        CHECK(bigcomplex_float_from_str(&back, buf, g_ctx, NULL)
                == BIGCOMPLEX_FLOAT_OK_E);
        if (bigfloat_is_nan(&z.re) || bigfloat_is_nan(&z.im)) {
            CHECK(bigfloat_is_nan(&back.re) && bigfloat_is_nan(&back.im));
        } else {
            CHECK(bigcomplex_float_eq(&back, &z));
        }
        bigcomplex_float_free(&back);
    }

    /* 缓冲不足 / 仅查询 */
    CHECK(parse(&z, "1+2i") == 0);
    size_t need = 0;
    CHECK(bigcomplex_float_to_str(&z, 0, NULL, 0, &need)
            == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(need == 5U);  /* "1+2i" + '\0' */
    char small[4];
    CHECK(bigcomplex_float_to_str(&z, 0, small, sizeof(small), &need)
            == BIGCOMPLEX_FLOAT_ERR_OVERFLOW_E);
    CHECK(bigcomplex_float_to_str(NULL, 0, NULL, 0, NULL)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);

    bigcomplex_float_free(&z);
}

static void test_arith(void) {
    bigcomplex_float_ty a;
    bigcomplex_float_ty b;
    bigcomplex_float_ty c;
    bigcomplex_float_init(&a);
    bigcomplex_float_init(&b);
    bigcomplex_float_init(&c);

    /* 整数分量精确运算 */
    CHECK(parse(&a, "1+2i") == 0);
    CHECK(parse(&b, "3+4i") == 0);
    bigcomplex_float_add(&c, &a, &b, g_ctx);
    expect_str("add", &c, "4+6i");
    bigcomplex_float_sub(&c, &a, &b, g_ctx);
    expect_str("sub", &c, "-2-2i");
    bigcomplex_float_mul(&c, &a, &b, g_ctx);
    expect_str("mul", &c, "-5+10i");   /* (1+2i)(3+4i) = -5+10i */
    bigcomplex_float_div(&c, &a, &a, g_ctx);
    expect_str("div self", &c, "1");   /* (1+2i)/(1+2i) = 1 */
    bigcomplex_float_div(&c, &a, &b, g_ctx);
    /* (1+2i)/(3+4i) = 11/25 + 2/25i → 0.44+0.08i */
    expect_str("div", &c, "0.44+0.08i");
    bigcomplex_float_mul(&c, &c, &b, g_ctx);
    CHECK(bigcomplex_float_eq(&c, &a));  /* 除法逆元 */

    /* 实部/虚部零操作数 */
    CHECK(parse(&a, "2+0i") == 0);
    CHECK(parse(&b, "0+3i") == 0);
    bigcomplex_float_mul(&c, &a, &b, g_ctx);
    expect_str("mul 纯实×纯虚", &c, "6i");
    bigcomplex_float_div(&c, &a, &b, g_ctx);
    expect_str("div 2/3i", &c, "-0.6666666666666666i");

    /* NULL / 别名安全 */
    CHECK(bigcomplex_float_add(NULL, &a, &b, g_ctx)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_add(&c, NULL, &b, g_ctx)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_add(&c, &a, &b, NULL)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);
    CHECK(bigcomplex_float_add(&c, &c, &c, g_ctx) == BIGCOMPLEX_FLOAT_OK_E);

    bigcomplex_float_free(&c);
    bigcomplex_float_free(&b);
    bigcomplex_float_free(&a);
}

static void test_abs_conj_arg(void) {
    bigcomplex_float_ty z;
    bigfloat_ty v;
    bigcomplex_float_init(&z);
    bigfloat_init(&v);

    /* |3+4i| = 5 */
    CHECK(parse(&z, "3+4i") == 0);
    CHECK(bigcomplex_float_abs(&v, &z, g_ctx) == BIGCOMPLEX_FLOAT_OK_E);
    expect_re("|3+4i|", &v, "5");
    /* |0| = 0 */
    CHECK(parse(&z, "0") == 0);
    bigcomplex_float_abs(&v, &z, g_ctx);
    expect_re("|0|", &v, "0");
    /* |inf+0i| = inf */
    CHECK(parse(&z, "inf") == 0);
    bigcomplex_float_abs(&v, &z, g_ctx);
    CHECK(bigfloat_is_inf(&v));
    /* |nan+1i| = nan */
    CHECK(parse(&z, "nan+1i") == 0);
    bigcomplex_float_abs(&v, &z, g_ctx);
    CHECK(bigfloat_is_nan(&v));
    /* |12+5i| = 13 */
    CHECK(parse(&z, "12+5i") == 0);
    bigcomplex_float_abs(&v, &z, g_ctx);
    expect_re("|12+5i|", &v, "13");

    /* conj */
    CHECK(parse(&z, "1+2i") == 0);
    CHECK(bigcomplex_float_conj(&z) == BIGCOMPLEX_FLOAT_OK_E);
    expect_str("conj", &z, "1-2i");
    CHECK(parse(&z, "1-2i") == 0);
    bigcomplex_float_conj(&z);
    expect_str("conj2", &z, "1+2i");
    CHECK(parse(&z, "3i") == 0);
    bigcomplex_float_conj(&z);
    expect_str("conj3", &z, "-3i");
    CHECK(parse(&z, "nan+1i") == 0);
    bigcomplex_float_conj(&z);
    expect_str("conj nan", &z, "nan-1i");  /* 虚部翻转，实部 NaN 不变 */
    CHECK(bigcomplex_float_conj(NULL) == BIGCOMPLEX_FLOAT_ERR_INVALID_E);

    /* arg → UNSUPPORTED（v1） */
    CHECK(parse(&z, "1+1i") == 0);
    CHECK(bigcomplex_float_arg(&v, &z, g_ctx)
            == BIGCOMPLEX_FLOAT_ERR_UNSUPPORTED_E);
    CHECK(bigcomplex_float_arg(NULL, &z, g_ctx)
            == BIGCOMPLEX_FLOAT_ERR_INVALID_E);

    bigfloat_free(&v);
    bigcomplex_float_free(&z);
}

static void test_special_eq(void) {
    bigcomplex_float_ty a;
    bigcomplex_float_ty b;
    bigcomplex_float_init(&a);
    bigcomplex_float_init(&b);

    /* NaN 传播（§9.3：任一分量 NaN → 整体 (NaN, NaN)） */
    CHECK(parse(&a, "nan+1i") == 0);
    CHECK(parse(&b, "1+1i") == 0);
    bigcomplex_float_add(&a, &a, &b, g_ctx);
    CHECK(bigfloat_is_nan(&a.re) && bigfloat_is_nan(&a.im));
    CHECK(parse(&a, "1+nani") == 0);
    CHECK(parse(&b, "2+0i") == 0);
    bigcomplex_float_mul(&a, &a, &b, g_ctx);
    CHECK(bigfloat_is_nan(&a.re) && bigfloat_is_nan(&a.im));

    /* ∞ 混合：∞ + (−∞) → NaN */
    CHECK(parse(&a, "inf+0i") == 0);
    CHECK(parse(&b, "-inf+0i") == 0);
    bigcomplex_float_add(&a, &a, &b, g_ctx);
    CHECK(bigfloat_is_nan(&a.re));
    /* 0 × ∞ → NaN */
    CHECK(parse(&a, "0+0i") == 0);
    CHECK(parse(&b, "inf+infi") == 0);
    bigcomplex_float_mul(&a, &a, &b, g_ctx);
    CHECK(bigfloat_is_nan(&a.re) && bigfloat_is_nan(&a.im));

    /* eq：NaN 分量 → false；±0 相等 */
    CHECK(parse(&a, "1+2i") == 0);
    CHECK(parse(&b, "1+2i") == 0);
    CHECK(bigcomplex_float_eq(&a, &b));
    CHECK(parse(&b, "1+3i") == 0);
    CHECK(!bigcomplex_float_eq(&a, &b));
    CHECK(parse(&a, "nan+0i") == 0);
    CHECK(parse(&b, "nan+0i") == 0);
    CHECK(!bigcomplex_float_eq(&a, &b));
    CHECK(parse(&a, "-0+0i") == 0);
    CHECK(parse(&b, "0+0i") == 0);
    CHECK(bigcomplex_float_eq(&a, &b));
    CHECK(!bigcomplex_float_eq(NULL, &b));
    CHECK(!bigcomplex_float_eq(&a, NULL));

    bigcomplex_float_free(&b);
    bigcomplex_float_free(&a);
}

int main(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    g_ctx = &ctx;

    test_lifecycle();
    test_parse_str();
    test_to_str();
    test_arith();
    test_abs_conj_arg();
    test_special_eq();

    if (g_fail == 0) {
        printf("test_bigcomplex_float: ALL PASS\n");
        return 0;
    }
    printf("test_bigcomplex_float: %d FAILURES\n", g_fail);
    return 1;
}
