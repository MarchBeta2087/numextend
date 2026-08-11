/* 单元测试：bigdecimal 全 API（生命周期/上下文、分类、构造与转换、
 * 四则/sqrt、比较、分解合成、舍入模式、错误路径与边界）。 */
#include "nex/bigdecimal/nex_bigdecimal.h"
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

/* ------------------------------------------------------------------ */
/* 辅助                                                                */
/* ------------------------------------------------------------------ */

static const char *flag_name(bigdecimal_flag_ty f) {
    switch (f) {
        case BIGDECIMAL_POS_ZERO_E: return "+0";
        case BIGDECIMAL_NEG_ZERO_E: return "-0";
        case BIGDECIMAL_POS_E: return "+";
        case BIGDECIMAL_NEG_E: return "-";
        case BIGDECIMAL_POS_INF_E: return "+inf";
        case BIGDECIMAL_NEG_INF_E: return "-inf";
        default: return "nan";
    }
}

static void expect_str(const char *desc, const bigdecimal_ty *v,
        const char *expect) {
    char buf[512];
    if (bigdecimal_to_str(v, BIGDECIMAL_FMT_FIXED_E, buf, sizeof(buf), NULL)
            != BIGDECIMAL_OK_E) {
        printf("FAIL %s: to_str error, expect %s\n", desc, expect);
        g_fail++;
        return;
    }
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static void expect_value(const char *desc, const bigdecimal_ty *v,
        bigdecimal_flag_ty flag, const char *mant_expect, int64_t exp_expect) {
    if (v->flag != flag) {
        printf("FAIL %s: flag %s, expect %s\n", desc, flag_name(v->flag),
                flag_name(flag));
        g_fail++;
        return;
    }
    if (flag != BIGDECIMAL_POS_E && flag != BIGDECIMAL_NEG_E) {
        if (bigint_dec_digit_len(&v->mant) != 0U || v->exp != 0) {
            printf("FAIL %s: special 值应 mant=0 exp=0\n", desc);
            g_fail++;
        }
        return;
    }
    size_t needed = 0;
    bigint_dec_to_str(&v->mant, 10, NULL, 0, &needed);
    char *buf = (char *)malloc(needed);
    if (buf == NULL) { CHECK(0); return; }
    bigint_dec_to_str(&v->mant, 10, buf, needed, &needed);
    if ((strcmp(buf, mant_expect) != 0) || (v->exp != exp_expect)) {
        printf("FAIL %s: got mant=%s exp=%lld, expect mant=%s exp=%lld\n",
                desc, buf, (long long)v->exp, mant_expect,
                (long long)exp_expect);
        g_fail++;
    }
    free(buf);
}

/* 规范化不变式（§8.1）：正常值尾数个位非 0；特殊值 mant/exp 置零 */
static void check_invariants(const char *desc, const bigdecimal_ty *v) {
    (void)desc;
    if (v->flag == BIGDECIMAL_POS_E || v->flag == BIGDECIMAL_NEG_E) {
        CHECK(bigint_dec_digit_len(&v->mant) > 0U);
        size_t needed = 0;
        bigint_dec_to_str(&v->mant, 10, NULL, 0, &needed);
        char *buf = (char *)malloc(needed);
        if (buf != NULL) {
            bigint_dec_to_str(&v->mant, 10, buf, needed, &needed);
            const size_t dl = strlen(buf);
            CHECK(dl > 0U);
            CHECK(buf[dl - 1U] != '0');  // 个位非 0
            free(buf);
        }
    } else {
        CHECK(bigint_dec_digit_len(&v->mant) == 0U);
        CHECK(v->exp == 0);
    }
}

static int parse(bigdecimal_ty *v, const char *s,
        const bigdecimal_ctx_ty *ctx) {
    return bigdecimal_from_str(v, s, ctx, NULL) == BIGDECIMAL_OK_E ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* 1. 生命周期与上下文                                                  */
/* ------------------------------------------------------------------ */

static void test_lifecycle(void) {
    bigdecimal_ty v;
    CHECK(bigdecimal_init(NULL) == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_init(&v) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_zero(&v));
    CHECK(v.flag == BIGDECIMAL_POS_ZERO_E);

    bigdecimal_ctx_ty ctx;
    CHECK(bigdecimal_ctx_make(NULL, 16, 3, BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_ctx_make(&ctx, 0, 3, BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            == BIGDECIMAL_ERR_INVALID_E);      /* mant_digits < 1 */
    CHECK(bigdecimal_ctx_make(&ctx, 16, 0, BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            == BIGDECIMAL_ERR_INVALID_E);      /* exp_digits == 0 */
    CHECK(bigdecimal_ctx_make(&ctx, 16, 19, BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            == BIGDECIMAL_ERR_INVALID_E);      /* exp_digits > 18 */
    CHECK(bigdecimal_ctx_make(&ctx, 16, 3, (bigdecimal_round_ty)99)
            == BIGDECIMAL_ERR_INVALID_E);      /* 非法舍入模式 */
    CHECK(bigdecimal_ctx_make(&ctx, 16, 3, BIGDECIMAL_ROUND_NEAREST_EVEN_E)
            == BIGDECIMAL_OK_E);
    CHECK(ctx.mant_digits == 16U && ctx.exp_digits == 3U);

    const bigdecimal_ctx_ty d32 = bigdecimal_ctx_decimal32();
    const bigdecimal_ctx_ty d64 = bigdecimal_ctx_decimal64();
    const bigdecimal_ctx_ty d128 = bigdecimal_ctx_decimal128();
    CHECK(d32.mant_digits == 7U && d32.exp_digits == 2U);
    CHECK(d64.mant_digits == 16U && d64.exp_digits == 3U);
    CHECK(d128.mant_digits == 34U && d128.exp_digits == 4U);

    /* 深拷贝 */
    bigdecimal_ty src;
    bigdecimal_init(&src);  /* 构造 API 要求 dst 已初始化（会先释放旧值） */
    CHECK(bigdecimal_from_str(&src, "-3.5", &d64, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_copy(NULL, &src) == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_copy(&v, NULL) == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_copy(&v, &src) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_cmp(&v, &src) == 0);
    CHECK(v.flag == BIGDECIMAL_NEG_E);
    bigdecimal_free(&v);
    bigdecimal_free(NULL);  /* NULL 安全 */
    bigdecimal_free(&src);
}

/* ------------------------------------------------------------------ */
/* 2. 分类                                                              */
/* ------------------------------------------------------------------ */

static void test_classify(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty v;
    bigdecimal_init(&v);

    CHECK(bigdecimal_is_zero(NULL) == false);
    CHECK(bigdecimal_is_inf(NULL) == false);
    CHECK(bigdecimal_is_nan(NULL) == false);
    CHECK(bigdecimal_is_normal(NULL) == false);

    CHECK(bigdecimal_is_zero(&v) && !bigdecimal_is_normal(&v));
    CHECK(bigdecimal_from_str(&v, "-0", &ctx, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_zero(&v) && v.flag == BIGDECIMAL_NEG_ZERO_E);
    CHECK(bigdecimal_from_str(&v, "inf", &ctx, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_inf(&v) && v.flag == BIGDECIMAL_POS_INF_E);
    CHECK(bigdecimal_from_str(&v, "-inf", &ctx, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_inf(&v) && v.flag == BIGDECIMAL_NEG_INF_E);
    CHECK(bigdecimal_from_str(&v, "nan", &ctx, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_nan(&v));
    CHECK(bigdecimal_from_str(&v, "2.5", &ctx, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_normal(&v) && v.flag == BIGDECIMAL_POS_E);
    CHECK(bigdecimal_from_str(&v, "-2.5", &ctx, NULL) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_normal(&v) && v.flag == BIGDECIMAL_NEG_E);

    bigdecimal_free(&v);
}

/* 便捷：科学计数输出断言（buf 生命周期在函数内） */
static void expect_sci(const bigdecimal_ty *v, const char *expect) {
    char buf[512];
    if (bigdecimal_to_str(v, BIGDECIMAL_FMT_SCIENTIFIC_E, buf, sizeof(buf),
            NULL) != BIGDECIMAL_OK_E) {
        printf("FAIL sci: to_str error, expect %s\n", expect);
        g_fail++;
        return;
    }
    if (strcmp(buf, expect) != 0) {
        printf("FAIL sci: got %s, expect %s\n", buf, expect);
        g_fail++;
    }
}

/* ------------------------------------------------------------------ */
/* 3. 字符串往返与精确输出                                              */
/* ------------------------------------------------------------------ */

static void test_str_roundtrip(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_init(&a);
    bigdecimal_init(&b);

    const char *strs[] = { "0", "-0", "1", "-1", "0.5", "-12.340",
            "1e100", "1.5e-300", "3.141592653589793238462643383279",
            "123456789.123456789", "inf", "-inf", "nan", "1e-324",
            "1.7976931348623157e308", "0.00123", "12300", "-2.5e-17",
            "10000000000000000", "9.999999999999999e15" };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        CHECK(parse(&a, strs[i], &ctx) == 0);
        /* 精确输出 → 回解析相等（无需最短往返算法） */
        char buf[600];
        CHECK(bigdecimal_to_str(&a, BIGDECIMAL_FMT_FIXED_E, buf,
                sizeof(buf), NULL) == BIGDECIMAL_OK_E);
        if (bigdecimal_is_nan(&a)) {
            CHECK(parse(&b, buf, &ctx) == 0);
            CHECK(bigdecimal_is_nan(&b));
        } else {
            CHECK(parse(&b, buf, &ctx) == 0);
            CHECK(bigdecimal_eq(&b, &a));
        }
        check_invariants("str", &a);
    }

    /* 已知输出（定点 / 科学计数） */
    bigdecimal_from_str(&a, "12.34", &ctx, NULL);
    expect_str("12.34", &a, "12.34");
    CHECK(bigdecimal_to_str(&a, BIGDECIMAL_FMT_SCIENTIFIC_E,
            (char[64]){ 0 }, 64, NULL) == BIGDECIMAL_OK_E);
    expect_sci(&a, "1.234e+1");
    bigdecimal_from_str(&a, "12300", &ctx, NULL);
    expect_str("12300", &a, "12300");
    expect_sci(&a, "1.23e+4");
    bigdecimal_from_str(&a, "0.00123", &ctx, NULL);
    expect_str("0.00123", &a, "0.00123");
    expect_sci(&a, "1.23e-3");
    bigdecimal_from_str(&a, "100", &ctx, NULL);
    expect_sci(&a, "1e+2");
    bigdecimal_from_str(&a, "-12.34", &ctx, NULL);
    expect_str("-12.34", &a, "-12.34");
    expect_sci(&a, "-1.234e+1");
    bigdecimal_from_str(&a, "0.5", &ctx, NULL);
    expect_sci(&a, "5e-1");
    bigdecimal_from_str(&a, "1.5", &ctx, NULL);
    expect_sci(&a, "1.5e+0");

    /* to_str 缓冲不足 / 仅查询 */
    bigdecimal_from_str(&a, "1e100", &ctx, NULL);
    size_t need = 0;
    CHECK(bigdecimal_to_str(&a, BIGDECIMAL_FMT_FIXED_E, NULL, 0, &need)
            == BIGDECIMAL_OK_E);
    CHECK(need == 102U);  /* "1" + 100 个零 + '\0' */
    char small[8];
    CHECK(bigdecimal_to_str(&a, BIGDECIMAL_FMT_FIXED_E, small,
            sizeof(small), &need) == BIGDECIMAL_ERR_OVERFLOW_E);
    CHECK(need == 102U);
    CHECK(bigdecimal_to_str(NULL, BIGDECIMAL_FMT_FIXED_E, NULL, 0, NULL)
            == BIGDECIMAL_ERR_INVALID_E);

    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

static void test_parse_errors(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty v;
    bigdecimal_init(&v);
    const char *end = NULL;

    /* 首字符即非法 → PARSE，dst 不变 */
    bigdecimal_from_str(&v, "1.5", &ctx, NULL);
    const bigdecimal_flag_ty before = v.flag;
    CHECK(bigdecimal_from_str(&v, "xyz", &ctx, NULL)
            == BIGDECIMAL_ERR_PARSE_E);
    CHECK(v.flag == before);
    CHECK(bigdecimal_from_str(&v, "", &ctx, NULL) == BIGDECIMAL_ERR_PARSE_E);
    CHECK(bigdecimal_from_str(&v, "-", &ctx, NULL) == BIGDECIMAL_ERR_PARSE_E);
    CHECK(bigdecimal_from_str(&v, ".", &ctx, NULL) == BIGDECIMAL_ERR_PARSE_E);
    /* 指数缺数字：数字部分已消费，'e' 不消费 → OK + 部分消费 */
    end = NULL;
    CHECK(bigdecimal_from_str(&v, "1e", &ctx, &end) == BIGDECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'e');
    end = NULL;
    CHECK(bigdecimal_from_str(&v, "1e+", &ctx, &end) == BIGDECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'e');
    CHECK(bigdecimal_from_str(&v, "+", &ctx, NULL) == BIGDECIMAL_ERR_PARSE_E);
    CHECK(bigdecimal_from_str(NULL, "1", &ctx, NULL)
            == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_from_str(&v, "1", NULL, NULL)
            == BIGDECIMAL_ERR_INVALID_E);

    /* 部分消费 */
    CHECK(bigdecimal_from_str(&v, "12.5abc", &ctx, &end) == BIGDECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'a');
    end = NULL;
    CHECK(bigdecimal_from_str(&v, "-3e2x", &ctx, &end) == BIGDECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'x');
    end = NULL;
    CHECK(bigdecimal_from_str(&v, "7e", &ctx, &end) == BIGDECIMAL_OK_E);
    CHECK(end != NULL && end[0] == 'e');

    /* + 号解析 */
    CHECK(parse(&v, "+5", &ctx) == 0);
    char buf[64];
    bigdecimal_to_str(&v, BIGDECIMAL_FMT_FIXED_E, buf, sizeof(buf), NULL);
    CHECK(strcmp(buf, "5") == 0);

    bigdecimal_free(&v);
}

/* ------------------------------------------------------------------ */
/* 4. 算术（已知十进制结果）                                            */
/* ------------------------------------------------------------------ */

static void test_arith(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_ty c;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&c);

    /* 十进制语义：0.1 + 0.2 = 0.3（无二进制误差） */
    parse(&a, "0.1", &ctx);
    parse(&b, "0.2", &ctx);
    bigdecimal_add(&c, &a, &b, &ctx);
    expect_str("0.1+0.2", &c, "0.3");
    bigdecimal_mul(&c, &a, &b, &ctx);
    expect_str("0.1*0.2", &c, "0.02");
    bigdecimal_div(&c, &a, &b, &ctx);
    expect_str("0.1/0.2", &c, "0.5");
    bigdecimal_sub(&c, &a, &b, &ctx);
    expect_str("0.1-0.2", &c, "-0.1");

    /* 跨指数加法：1e10 + 1 = 10000000001 */
    parse(&a, "1e10", &ctx);
    parse(&b, "1", &ctx);
    bigdecimal_add(&c, &a, &b, &ctx);
    expect_str("1e10+1", &c, "10000000001");

    /* 大数乘法精确（16 位内） */
    parse(&a, "1234567891234567", &ctx);
    parse(&b, "2", &ctx);
    bigdecimal_mul(&c, &a, &b, &ctx);
    expect_str("大数×2", &c, "2469135782469134");

    /* 除法舍入：1/3 到 16 位 */
    parse(&a, "1", &ctx);
    parse(&b, "3", &ctx);
    bigdecimal_div(&c, &a, &b, &ctx);
    expect_str("1/3", &c, "0.3333333333333333");

    /* sqrt */
    parse(&a, "2", &ctx);
    bigdecimal_sqrt(&c, &a, &ctx);
    expect_str("sqrt(2)", &c, "1.414213562373095");
    parse(&a, "4", &ctx);
    bigdecimal_sqrt(&c, &a, &ctx);
    expect_str("sqrt(4)", &c, "2");
    parse(&a, "15241578750190521", &ctx);  /* 123456789² */
    bigdecimal_sqrt(&c, &a, &ctx);
    expect_str("sqrt(123456789²)", &c, "123456789");
    parse(&a, "0.25", &ctx);
    bigdecimal_sqrt(&c, &a, &ctx);
    expect_str("sqrt(0.25)", &c, "0.5");
    /* 完全平方数精确（长尾数） */
    parse(&a, "123456789123456789", &ctx);
    bigdecimal_mul(&c, &a, &a, &ctx);
    bigdecimal_sqrt(&c, &c, &ctx);
    CHECK(bigdecimal_eq(&c, &a));

    /* NULL / 别名安全 */
    CHECK(bigdecimal_add(NULL, &a, &b, &ctx) == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_add(&c, NULL, &b, &ctx) == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_add(&c, &a, &b, NULL) == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_add(&c, &c, &c, &ctx) == BIGDECIMAL_OK_E);

    bigdecimal_free(&c);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

/* ------------------------------------------------------------------ */
/* 5. 特殊值传播                                                        */
/* ------------------------------------------------------------------ */

static void test_special(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_ty c;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&c);

    parse(&a, "inf", &ctx);
    parse(&b, "-inf", &ctx);
    bigdecimal_add(&c, &a, &b, &ctx);
    CHECK(bigdecimal_is_nan(&c));                  /* +∞ + −∞ = NaN */
    bigdecimal_sub(&c, &a, &a, &ctx);
    CHECK(bigdecimal_is_nan(&c));                  /* +∞ − +∞ = NaN */
    bigdecimal_add(&c, &a, &a, &ctx);
    CHECK(bigdecimal_is_inf(&c) && c.flag == BIGDECIMAL_POS_INF_E);

    parse(&a, "5", &ctx);
    parse(&b, "0", &ctx);
    bigdecimal_mul(&c, &a, &b, &ctx);
    CHECK(bigdecimal_is_zero(&c) && c.flag == BIGDECIMAL_POS_ZERO_E);
    bigdecimal_from_str(&a, "inf", &ctx, NULL);
    bigdecimal_mul(&c, &a, &b, &ctx);
    CHECK(bigdecimal_is_nan(&c));                  /* ∞ × 0 = NaN */
    bigdecimal_div(&c, &a, &b, &ctx);
    CHECK(bigdecimal_is_inf(&c) && c.flag == BIGDECIMAL_POS_INF_E);
    bigdecimal_div(&c, &b, &b, &ctx);
    CHECK(bigdecimal_is_nan(&c));                  /* 0/0 = NaN */
    bigdecimal_from_str(&a, "-5", &ctx, NULL);
    bigdecimal_div(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_NEG_INF_E);         /* −5/0 = −∞ */
    bigdecimal_sqrt(&c, &a, &ctx);
    CHECK(bigdecimal_is_nan(&c));                  /* sqrt(−5) = NaN */
    bigdecimal_sqrt(&c, &b, &ctx);
    CHECK(bigdecimal_is_zero(&c) && c.flag == BIGDECIMAL_POS_ZERO_E);
    bigdecimal_from_str(&b, "-0", &ctx, NULL);
    bigdecimal_sqrt(&c, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_NEG_ZERO_E);        /* sqrt(−0) = −0 */

    /* NaN 吸收 */
    parse(&a, "nan", &ctx);
    parse(&b, "1.5", &ctx);
    bigdecimal_add(&c, &a, &b, &ctx);
    CHECK(bigdecimal_is_nan(&c));
    bigdecimal_mul(&c, &a, &b, &ctx);
    CHECK(bigdecimal_is_nan(&c));
    bigdecimal_div(&c, &b, &a, &ctx);
    CHECK(bigdecimal_is_nan(&c));
    bigdecimal_sqrt(&c, &a, &ctx);
    CHECK(bigdecimal_is_nan(&c));

    /* ±0/±∞ 操作数的符号传播 */
    bigdecimal_from_str(&a, "-0", &ctx, NULL);
    bigdecimal_from_str(&b, "2", &ctx, NULL);
    bigdecimal_mul(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_NEG_ZERO_E);
    bigdecimal_from_str(&a, "-inf", &ctx, NULL);
    bigdecimal_mul(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_NEG_INF_E);

    bigdecimal_free(&c);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

/* 零符号规则（§11） */
static void test_zero_sign(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_ty c;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&c);

    bigdecimal_from_str(&a, "-0", &ctx, NULL);
    bigdecimal_from_str(&b, "0", &ctx, NULL);
    bigdecimal_add(&c, &a, &a, &ctx);
    CHECK(c.flag == BIGDECIMAL_NEG_ZERO_E);
    bigdecimal_add(&c, &b, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_POS_ZERO_E);
    bigdecimal_add(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_POS_ZERO_E);
    bigdecimal_ctx_ty ctx_neg;
    bigdecimal_ctx_make(&ctx_neg, 16, 3, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    bigdecimal_add(&c, &a, &b, &ctx_neg);
    CHECK(c.flag == BIGDECIMAL_NEG_ZERO_E);

    /* x − x = +0（最近）/ −0（向 −∞） */
    bigdecimal_from_str(&a, "3.5", &ctx, NULL);
    bigdecimal_sub(&c, &a, &a, &ctx);
    CHECK(c.flag == BIGDECIMAL_POS_ZERO_E);
    bigdecimal_sub(&c, &a, &a, &ctx_neg);
    CHECK(c.flag == BIGDECIMAL_NEG_ZERO_E);

    /* 负 × 正 = 负零 */
    bigdecimal_from_str(&a, "-0", &ctx, NULL);
    bigdecimal_from_str(&b, "2", &ctx, NULL);
    bigdecimal_mul(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGDECIMAL_NEG_ZERO_E);

    bigdecimal_free(&c);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

/* ------------------------------------------------------------------ */
/* 6. 舍入模式与上下溢                                                  */
/* ------------------------------------------------------------------ */

static void test_rounding_modes(void) {
    bigdecimal_ctx_ty ctx;
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_ty c;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&c);

    /* 向零截断：1.00000005 在 7 位下 → 1.000000，规范化后输出 "1" */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_TOWARD_ZERO_E);
    bigdecimal_from_str(&a, "1.00000005", &ctx, NULL);
    expect_str("toward-zero", &a, "1");
    /* 最近偶：同上 → 1 */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_from_str(&a, "1.00000005", &ctx, NULL);
    expect_str("nearest", &a, "1");
    /* ties-to-even（无粘位）：1.0000005 → 舍入位 5、低位全零、截断尾数
     * 为 0（偶）→ 不舍入 → 1.000000 → "1" */
    bigdecimal_from_str(&a, "1.0000005", &ctx, NULL);
    expect_str("nearest tie-down", &a, "1");
    /* ties-to-even（有粘位）：1.0000005000001 → 进位 → 1.000001 */
    bigdecimal_from_str(&a, "1.0000005000001", &ctx, NULL);
    expect_str("nearest tie-up", &a, "1.000001");
    /* 远离零：1.00000005 → 1.000001 */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_AWAY_ZERO_E);
    bigdecimal_from_str(&a, "1.00000005", &ctx, NULL);
    expect_str("away", &a, "1.000001");
    /* 向 +∞：1.00000001 → 1.000001 */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_TOWARD_POS_E);
    bigdecimal_from_str(&a, "1.00000001", &ctx, NULL);
    expect_str("toward-pos", &a, "1.000001");
    /* 向 +∞ 对负数：−1.00000001 → −1.000000（幅值向下）→ "-1" */
    bigdecimal_from_str(&a, "-1.00000001", &ctx, NULL);
    expect_str("toward-pos neg", &a, "-1");

    /* 上溢：最近 → ±∞；向零 → 最大有限值 9999999×10^99 */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_from_str(&a, "1e101", &ctx, NULL);
    CHECK(bigdecimal_is_inf(&a));  /* emax = 99，1e101 归一化指数 101 > 99 */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_TOWARD_ZERO_E);
    bigdecimal_from_str(&a, "1e101", &ctx, NULL);
    CHECK(bigdecimal_is_normal(&a));
    /* 最大有限值 = (10^7 − 1) × 10^99 = 9999999×10^99 */
    expect_value("max finite", &a, BIGDECIMAL_POS_E, "9999999", 99);

    /* 下溢：flush-to-zero */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_from_str(&a, "1e-101", &ctx, NULL);
    CHECK(bigdecimal_is_zero(&a) && a.flag == BIGDECIMAL_POS_ZERO_E);
    /* 向 −∞ 的负下溢 → −0 */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_TOWARD_NEG_E);
    bigdecimal_from_str(&a, "-1e-101", &ctx, NULL);
    CHECK(bigdecimal_is_zero(&a) && a.flag == BIGDECIMAL_NEG_ZERO_E);
    /* 向 +∞ 的负下溢 → +0（IEEE §7.5） */
    bigdecimal_ctx_make(&ctx, 7, 2, BIGDECIMAL_ROUND_TOWARD_POS_E);
    bigdecimal_from_str(&a, "-1e-101", &ctx, NULL);
    CHECK(bigdecimal_is_zero(&a) && a.flag == BIGDECIMAL_POS_ZERO_E);

    bigdecimal_free(&c);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

/* ------------------------------------------------------------------ */
/* 7. 比较                                                              */
/* ------------------------------------------------------------------ */

static void test_cmp(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_init(&a);
    bigdecimal_init(&b);

    bigdecimal_from_str(&a, "1", &ctx, NULL);
    bigdecimal_from_str(&b, "0.5", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);
    CHECK(bigdecimal_cmp(&b, &a) < 0);
    bigdecimal_from_str(&a, "1e100", &ctx, NULL);
    bigdecimal_from_str(&b, "1e-100", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);
    bigdecimal_from_str(&a, "-1e100", &ctx, NULL);
    bigdecimal_from_str(&b, "1e-100", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) < 0);
    bigdecimal_from_str(&a, "1.000000000000001", &ctx, NULL);
    bigdecimal_from_str(&b, "1", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);   /* 尾数仅差 1 ulp */

    /* 跨指数：区间重叠（指数差 < 位数差）走精确对齐比较 */
    bigdecimal_from_str(&a, "3", &ctx, NULL);
    bigdecimal_from_str(&b, "2.5", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);   /* 3 > 2.5 */
    bigdecimal_from_str(&a, "123456789123456", &ctx, NULL);
    bigdecimal_from_str(&b, "123456789123455", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);
    bigdecimal_from_str(&a, "1", &ctx, NULL);
    bigdecimal_from_str(&b, "0.9999999999999999", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);   /* 1 > 0.9999...9 */

    /* 全序与 NaN */
    bigdecimal_from_str(&a, "-inf", &ctx, NULL);
    bigdecimal_from_str(&b, "inf", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) < 0);
    bigdecimal_from_str(&a, "0", &ctx, NULL);
    bigdecimal_from_str(&b, "-0", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) == 0);
    CHECK(bigdecimal_eq(&a, &b));
    bigdecimal_from_str(&a, "nan", &ctx, NULL);
    bigdecimal_from_str(&b, "1", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) == 2);
    CHECK(bigdecimal_cmp(&a, &a) == 2);
    CHECK(!bigdecimal_eq(&a, &a));
    bigdecimal_from_str(&a, "3.5", &ctx, NULL);
    bigdecimal_from_str(&b, "-3.5", &ctx, NULL);
    CHECK(bigdecimal_cmp(&a, &b) > 0);

    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

/* ------------------------------------------------------------------ */
/* 8. decompose / compose / neg / from_bigint                          */
/* ------------------------------------------------------------------ */

static void test_decompose_compose(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty v;
    bigdecimal_ty w;
    bigint_dec_ty mant;
    bigint_dec_init(&mant);
    bigdecimal_init(&v);
    bigdecimal_init(&w);

    bigdecimal_from_str(&v, "-123.456", &ctx, NULL);
    int64_t exp = 0;
    bigdecimal_flag_ty flag = BIGDECIMAL_NAN_E;
    CHECK(bigdecimal_decompose(&v, &mant, &exp, &flag) == BIGDECIMAL_OK_E);
    CHECK(flag == BIGDECIMAL_NEG_E);
    CHECK(bigint_dec_digit_len(&mant) == 6U);  /* 123456 */
    CHECK(exp == -3);
    CHECK(bigdecimal_compose(&w, &mant, exp, flag, &ctx) == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_eq(&w, &v));

    /* 任意尾数合成 → 按 ctx 舍入规范化 */
    bigint_dec_ty big;
    bigint_dec_init(&big);
    bigint_dec_from_str(&big, "123456789123456789123456789", 10, NULL);
    CHECK(bigdecimal_compose(&w, &big, 0, BIGDECIMAL_POS_E, &ctx)
            == BIGDECIMAL_OK_E);
    check_invariants("compose", &w);
    /* 特殊值合成忽略 mant/exp */
    CHECK(bigdecimal_compose(&w, &big, 123, BIGDECIMAL_NAN_E, &ctx)
            == BIGDECIMAL_OK_E);
    CHECK(bigdecimal_is_nan(&w));
    /* NULL / 非法 flag */
    CHECK(bigdecimal_decompose(NULL, &mant, &exp, &flag)
            == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_decompose(&v, NULL, &exp, &flag)
            == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_compose(NULL, &big, 0, BIGDECIMAL_POS_E, &ctx)
            == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_compose(&w, NULL, 0, BIGDECIMAL_POS_E, &ctx)
            == BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_compose(&w, &big, 0, (bigdecimal_flag_ty)77, &ctx)
            == BIGDECIMAL_ERR_INVALID_E);

    bigint_dec_free(&big);
    bigint_dec_free(&mant);
    bigdecimal_free(&w);
    bigdecimal_free(&v);
}

static void test_neg_from_bigint(void) {
    bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty v;
    bigdecimal_init(&v);
    bigint_dec_ty n;
    bigint_dec_init(&n);

    bigdecimal_from_str(&v, "2.5", &ctx, NULL);
    CHECK(bigdecimal_neg(&v) == BIGDECIMAL_OK_E);
    CHECK(v.flag == BIGDECIMAL_NEG_E);
    CHECK(bigdecimal_neg(&v) == BIGDECIMAL_OK_E);
    CHECK(v.flag == BIGDECIMAL_POS_E);
    bigdecimal_from_str(&v, "-0", &ctx, NULL);
    bigdecimal_neg(&v);
    CHECK(v.flag == BIGDECIMAL_POS_ZERO_E);
    bigdecimal_from_str(&v, "inf", &ctx, NULL);
    bigdecimal_neg(&v);
    CHECK(v.flag == BIGDECIMAL_NEG_INF_E);
    bigdecimal_from_str(&v, "nan", &ctx, NULL);
    bigdecimal_neg(&v);
    CHECK(bigdecimal_is_nan(&v));
    CHECK(bigdecimal_neg(NULL) == BIGDECIMAL_ERR_INVALID_E);

    /* from_bigint：精确与舍入 */
    bigint_dec_from_str(&n, "123456789", 10, NULL);
    CHECK(bigdecimal_from_bigint(&v, &n, &ctx) == BIGDECIMAL_OK_E);
    expect_str("from_bigint", &v, "123456789");
    /* 超长整数 → 按 ctx 舍入 */
    bigint_dec_ty big;
    bigint_dec_init(&big);
    bigint_dec_from_str(&big, "123456789123456789123456789", 10, NULL);
    CHECK(bigdecimal_from_bigint(&v, &big, &ctx) == BIGDECIMAL_OK_E);
    check_invariants("from_bigint round", &v);
    /* 负整数 */
    bigint_dec_from_str(&big, "-9999999999999999", 10, NULL);
    CHECK(bigdecimal_from_bigint(&v, &big, &ctx) == BIGDECIMAL_OK_E);
    CHECK(v.flag == BIGDECIMAL_NEG_E);
    bigint_dec_free(&big);

    bigint_dec_free(&n);
    bigdecimal_free(&v);
}

/* ------------------------------------------------------------------ */
/* 9. 极小上下文边界                                                    */
/* ------------------------------------------------------------------ */

static void test_min_ctx(void) {
    bigdecimal_ctx_ty ctx;
    bigdecimal_ty a;
    bigdecimal_ty b;
    bigdecimal_ty c;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&c);

    /* mant_digits = 1：1.5 → 2（最近偶）；1.4 → 1 */
    bigdecimal_ctx_make(&ctx, 1, 2, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_from_str(&a, "1.5", &ctx, NULL);
    expect_str("p1 1.5", &a, "2");
    bigdecimal_from_str(&a, "1.4", &ctx, NULL);
    expect_str("p1 1.4", &a, "1");
    bigdecimal_from_str(&a, "0.5", &ctx, NULL);
    expect_str("p1 0.5", &a, "0.5");  /* 单数字 5 × 10^-1，无需舍入 */
    /* exp_digits = 1：范围 ±9 */
    bigdecimal_ctx_make(&ctx, 1, 1, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
    bigdecimal_from_str(&a, "1e9", &ctx, NULL);   /* 指数 9 ≤ emax 9 ✓ */
    CHECK(bigdecimal_is_normal(&a));
    bigdecimal_from_str(&a, "1e10", &ctx, NULL);  /* 指数 10 > emax → 上溢 */
    CHECK(bigdecimal_is_inf(&a));
    bigdecimal_from_str(&a, "1e-10", &ctx, NULL); /* 指数 −10 < emin → 下溢 */
    CHECK(bigdecimal_is_zero(&a));

    bigdecimal_free(&c);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

/* ------------------------------------------------------------------ */

int main(void) {
#ifdef _MSC_VER
    /* CRT 调试堆：逐次分配完整性检查 + 退出时泄漏报告 */
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_CRT_DF
            | _CRTDBG_LEAK_CHECK_DF);
#endif

    test_lifecycle();
    test_classify();
    test_str_roundtrip();
    test_parse_errors();
    test_arith();
    test_special();
    test_zero_sign();
    test_rounding_modes();
    test_cmp();
    test_decompose_compose();
    test_neg_from_bigint();
    test_min_ctx();

    if (g_fail == 0) {
        printf("test_bigdecimal: ALL PASS\n");
        return 0;
    }
    printf("test_bigdecimal: %d FAILURES\n", g_fail);
    return 1;
}
