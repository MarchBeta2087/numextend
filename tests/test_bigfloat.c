/* 单元测试：bigfloat 全 API（生命周期/上下文、分类、构造与转换、
 * 四则/sqrt、比较、分解合成、舍入模式、错误路径与边界）。 */
#include "nex/bigfloat/nex_bigfloat.h"
#include <math.h>
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

static const char *flag_name(bigfloat_flag_ty f) {
    switch (f) {
        case BIGFLOAT_POS_ZERO_E: return "+0";
        case BIGFLOAT_NEG_ZERO_E: return "-0";
        case BIGFLOAT_POS_E: return "+";
        case BIGFLOAT_NEG_E: return "-";
        case BIGFLOAT_POS_INF_E: return "+inf";
        case BIGFLOAT_NEG_INF_E: return "-inf";
        default: return "nan";
    }
}

static void expect_str(const char *desc, const bigfloat_ty *v,
        const char *expect) {
    char buf[512];
    if (bigfloat_to_str(v, 0, buf, sizeof(buf), NULL) != BIGFLOAT_OK_E) {
        printf("FAIL %s: to_str error, expect %s\n", desc, expect);
        g_fail++;
        return;
    }
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static void expect_value(const char *desc, const bigfloat_ty *v,
        bigfloat_flag_ty flag, const char *mant_expect, int64_t exp_expect) {
    if (v->flag != flag) {
        printf("FAIL %s: flag %s, expect %s\n", desc, flag_name(v->flag),
                flag_name(flag));
        g_fail++;
        return;
    }
    if (flag != BIGFLOAT_POS_E && flag != BIGFLOAT_NEG_E) {
        if (bigint_bin_bit_len(&v->mant) != 0U || v->exp != 0) {
            printf("FAIL %s: special 值应 mant=0 exp=0\n", desc);
            g_fail++;
        }
        return;
    }
    size_t needed = 0;
    bigint_bin_to_str(&v->mant, 10, NULL, 0, &needed);
    char *buf = (char *)malloc(needed);
    if (buf == NULL) { CHECK(0); return; }
    bigint_bin_to_str(&v->mant, 10, buf, needed, &needed);
    if ((strcmp(buf, mant_expect) != 0) || (v->exp != exp_expect)) {
        printf("FAIL %s: got mant=%s exp=%lld, expect mant=%s exp=%lld\n",
                desc, buf, (long long)v->exp, mant_expect,
                (long long)exp_expect);
        g_fail++;
    }
    free(buf);
}

/* 规范化不变式（§3.4）：正常值尾数最高位恒 1 且非零；特殊值 mant/exp 置零 */
static void check_invariants(const char *desc, const bigfloat_ty *v) {
    (void)desc;
    if (v->flag == BIGFLOAT_POS_E || v->flag == BIGFLOAT_NEG_E) {
        const size_t bl = bigint_bin_bit_len(&v->mant);
        CHECK(bl > 0U);
        CHECK(bigint_bin_bit_test(&v->mant, bl - 1U));
        CHECK(bigint_bin_sign(&v->mant) == BIGINT_SIGN_POS_E);
    } else {
        CHECK(bigint_bin_bit_len(&v->mant) == 0U);
        CHECK(v->exp == 0);
    }
}

static int parse(bigfloat_ty *v, const char *s, const bigfloat_ctx_ty *ctx) {
    return bigfloat_from_str(v, s, ctx, NULL) == BIGFLOAT_OK_E ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* 1. 生命周期与上下文                                                  */
/* ------------------------------------------------------------------ */

static void test_lifecycle(void) {
    bigfloat_ty v;
    CHECK(bigfloat_init(NULL) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_init(&v) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_zero(&v));
    CHECK(v.flag == BIGFLOAT_POS_ZERO_E);

    bigfloat_ctx_ty ctx;
    CHECK(bigfloat_ctx_make(NULL, 53, 11, BIGFLOAT_ROUND_NEAREST_EVEN_E)
            == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_ctx_make(&ctx, 1, 11, BIGFLOAT_ROUND_NEAREST_EVEN_E)
            == BIGFLOAT_ERR_INVALID_E);      /* mant_bits < 2 */
    CHECK(bigfloat_ctx_make(&ctx, 53, 0, BIGFLOAT_ROUND_NEAREST_EVEN_E)
            == BIGFLOAT_ERR_INVALID_E);      /* exp_bits == 0 */
    CHECK(bigfloat_ctx_make(&ctx, 53, 62, BIGFLOAT_ROUND_NEAREST_EVEN_E)
            == BIGFLOAT_ERR_INVALID_E);      /* exp_bits > 61 */
    CHECK(bigfloat_ctx_make(&ctx, 53, 11, (bigfloat_round_ty)99)
            == BIGFLOAT_ERR_INVALID_E);      /* 非法舍入模式 */
    CHECK(bigfloat_ctx_make(&ctx, 53, 11, BIGFLOAT_ROUND_NEAREST_EVEN_E)
            == BIGFLOAT_OK_E);
    CHECK(ctx.mant_bits == 53U && ctx.exp_bits == 11U);

    const bigfloat_ctx_ty c32 = bigfloat_ctx_binary32();
    const bigfloat_ctx_ty c64 = bigfloat_ctx_binary64();
    const bigfloat_ctx_ty c128 = bigfloat_ctx_binary128();
    CHECK(c32.mant_bits == 24U && c32.exp_bits == 8U);
    CHECK(c64.mant_bits == 53U && c64.exp_bits == 11U);
    CHECK(c128.mant_bits == 113U && c128.exp_bits == 15U);

    /* 深拷贝 */
    bigfloat_ty src;
    bigfloat_init(&src);  /* 构造 API 要求 dst 已初始化（会先释放旧值） */
    CHECK(bigfloat_from_str(&src, "-3.5", &c64, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_copy(NULL, &src) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_copy(&v, NULL) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_copy(&v, &src) == BIGFLOAT_OK_E);
    CHECK(bigfloat_cmp(&v, &src) == 0);
    CHECK(v.flag == BIGFLOAT_NEG_E);
    bigfloat_free(&v);
    bigfloat_free(NULL);  /* NULL 安全 */
    bigfloat_free(&src);
}

/* ------------------------------------------------------------------ */
/* 2. 分类                                                              */
/* ------------------------------------------------------------------ */

static void test_classify(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty v;
    bigfloat_init(&v);

    CHECK(bigfloat_is_zero(NULL) == false);
    CHECK(bigfloat_is_inf(NULL) == false);
    CHECK(bigfloat_is_nan(NULL) == false);
    CHECK(bigfloat_is_normal(NULL) == false);

    CHECK(bigfloat_is_zero(&v) && !bigfloat_is_normal(&v));
    CHECK(bigfloat_from_str(&v, "-0", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_zero(&v) && v.flag == BIGFLOAT_NEG_ZERO_E);
    CHECK(bigfloat_from_str(&v, "inf", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_inf(&v) && v.flag == BIGFLOAT_POS_INF_E);
    CHECK(bigfloat_from_str(&v, "-inf", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_inf(&v) && v.flag == BIGFLOAT_NEG_INF_E);
    CHECK(bigfloat_from_str(&v, "nan", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_nan(&v));
    CHECK(bigfloat_from_str(&v, "2.5", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_normal(&v) && v.flag == BIGFLOAT_POS_E);
    CHECK(bigfloat_from_str(&v, "-2.5", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_normal(&v) && v.flag == BIGFLOAT_NEG_E);

    bigfloat_free(&v);
}

/* ------------------------------------------------------------------ */
/* 3. 构造与转换                                                        */
/* ------------------------------------------------------------------ */

static void test_from_to_f64(void) {
    bigfloat_ty v;
    bigfloat_init(&v);
    const bigfloat_ctx_ty c64 = bigfloat_ctx_binary64();
    const double vals[] = { 0.0, -0.0, 1.0, -1.0, 0.5, 3.141592653589793,
            1e300, 1e-300, 5e-324, 2.2250738585072014e-308,
            1.7976931348623157e308, 1.0 / 0.0, -1.0 / 0.0 };
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        CHECK(bigfloat_from_f64(&v, vals[i]) == BIGFLOAT_OK_E);
        double out = 0.0;
        CHECK(bigfloat_to_f64(&v, &out) == BIGFLOAT_OK_E);
        if (vals[i] != vals[i]) {
            CHECK(out != out);  /* NaN */
        } else if (vals[i] == 0.0) {
            CHECK(out == 0.0);
            CHECK((vals[i] < 0.0) == (out < 0.0));
        } else {
            CHECK(out == vals[i]);
        }
        check_invariants("f64", &v);
    }
    /* NULL 参数 */
    CHECK(bigfloat_from_f64(NULL, 1.0) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_to_f64(&v, NULL) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_to_f64(NULL, NULL) == BIGFLOAT_ERR_INVALID_E);

    /* to_f64 溢出 → ±HUGE_VAL + OVERFLOW；下溢 → ±0 */
    bigfloat_ctx_ty big;
    bigfloat_ctx_make(&big, 53, 20, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_ty huge;
    bigfloat_init(&huge);
    CHECK(bigfloat_from_str(&huge, "1e300", &big, NULL) == BIGFLOAT_OK_E);
    /* 1e300 × 1e300 = 1e600 > double 范围 */
    bigfloat_ty r;
    bigfloat_init(&r);
    CHECK(bigfloat_mul(&r, &huge, &huge, &big) == BIGFLOAT_OK_E);
    double out = 0.0;
    CHECK(bigfloat_to_f64(&r, &out) == BIGFLOAT_ERR_OVERFLOW_E);
    CHECK(out == HUGE_VAL);
    bigfloat_neg(&r);
    CHECK(bigfloat_to_f64(&r, &out) == BIGFLOAT_ERR_OVERFLOW_E);
    CHECK(out == -HUGE_VAL);
    /* 下溢：极小值 → 0（binary64 ctx 的 emin = −1024，1e-400 超范围） */
    bigfloat_ty tiny;
    bigfloat_init(&tiny);
    bigfloat_from_str(&tiny, "1e-400", &c64, NULL);
    CHECK(bigfloat_is_zero(&tiny));  /* ctx 范围外 → flush */
    bigfloat_to_f64(&tiny, &out);
    CHECK(out == 0.0);

    bigfloat_free(&tiny);
    bigfloat_free(&r);
    bigfloat_free(&huge);
    bigfloat_free(&v);
}

static void test_str_roundtrip(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_init(&a);
    bigfloat_init(&b);

    const char *strs[] = { "0", "-0", "1", "-1", "0.5", "-12.340",
            "1e100", "1.5e-300", "3.141592653589793238462643383279",
            "123456789.123456789", "inf", "-inf", "nan", "1e-324",
            "1.7976931348623157e308", "1.0000000000000002",
            "9007199254740993", "1e-100", "1e+300", "-2.5e-17" };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        CHECK(parse(&a, strs[i], &ctx) == 0);
        char buf[512];
        CHECK(bigfloat_to_str(&a, 0, buf, sizeof(buf), NULL) == BIGFLOAT_OK_E);
        if (bigfloat_is_nan(&a)) {
            CHECK(parse(&b, buf, &ctx) == 0);
            CHECK(bigfloat_is_nan(&b));
        } else {
            CHECK(parse(&b, buf, &ctx) == 0);
            CHECK(bigfloat_eq(&b, &a));
        }
        check_invariants("str", &a);
    }

    /* 已知最短输出 */
    bigfloat_from_str(&a, "1", &ctx, NULL);
    expect_str("1", &a, "1");
    bigfloat_from_str(&a, "0.5", &ctx, NULL);
    expect_str("0.5", &a, "0.5");
    bigfloat_from_str(&a, "-12.34", &ctx, NULL);
    expect_str("-12.34", &a, "-12.34");
    bigfloat_from_str(&a, "3.141592653589793238462643383279", &ctx, NULL);
    expect_str("pi", &a, "3.141592653589793");
    bigfloat_from_str(&a, "1e100", &ctx, NULL);
    expect_str("1e100", &a, "1e+100");
    bigfloat_from_str(&a, "1.7976931348623157e308", &ctx, NULL);
    expect_str("dblmax", &a, "1.7976931348623157e+308");

    /* to_str 缓冲不足 / 仅查询 */
    bigfloat_from_str(&a, "1e100", &ctx, NULL);
    size_t need = 0;
    CHECK(bigfloat_to_str(&a, 0, NULL, 0, &need) == BIGFLOAT_OK_E);
    CHECK(need == 7U);  /* "1e+100" + '\0' */
    char small[4];
    CHECK(bigfloat_to_str(&a, 0, small, sizeof(small), &need)
            == BIGFLOAT_ERR_OVERFLOW_E);
    CHECK(need == 7U);
    CHECK(bigfloat_to_str(NULL, 0, NULL, 0, NULL) == BIGFLOAT_ERR_INVALID_E);

    /* max_digits 限制：低于最短位数时退而求其次输出受限精度近似 */
    bigfloat_from_str(&a, "1.2345678901234567", &ctx, NULL);
    char small8[9];
    CHECK(bigfloat_to_str(&a, 8, small8, sizeof(small8), NULL)
            == BIGFLOAT_ERR_OVERFLOW_E);  /* "1.2345679" 9 字符 + '\0' = 10 > 9 */
    char mid[64];
    CHECK(bigfloat_to_str(&a, 8, mid, sizeof(mid), NULL) == BIGFLOAT_OK_E);
    CHECK(strcmp(mid, "1.2345679") == 0);

    bigfloat_free(&b);
    bigfloat_free(&a);
}

static void test_parse_errors(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty v;
    bigfloat_init(&v);
    const char *end = NULL;

    /* 首字符即非法 → PARSE，dst 不变 */
    bigfloat_from_str(&v, "1.5", &ctx, NULL);
    const bigfloat_flag_ty before = v.flag;
    CHECK(bigfloat_from_str(&v, "xyz", &ctx, NULL) == BIGFLOAT_ERR_PARSE_E);
    CHECK(v.flag == before);
    CHECK(bigfloat_from_str(&v, "", &ctx, NULL) == BIGFLOAT_ERR_PARSE_E);
    CHECK(bigfloat_from_str(&v, "-", &ctx, NULL) == BIGFLOAT_ERR_PARSE_E);
    CHECK(bigfloat_from_str(&v, ".", &ctx, NULL) == BIGFLOAT_ERR_PARSE_E);
    /* 指数缺数字：数字部分已消费，'e' 不消费 → OK + 部分消费 */
    end = NULL;
    CHECK(bigfloat_from_str(&v, "1e", &ctx, &end) == BIGFLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'e');
    end = NULL;
    CHECK(bigfloat_from_str(&v, "1e+", &ctx, &end) == BIGFLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'e');
    CHECK(bigfloat_from_str(&v, "+", &ctx, NULL) == BIGFLOAT_ERR_PARSE_E);
    CHECK(bigfloat_from_str(NULL, "1", &ctx, NULL) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_from_str(&v, "1", NULL, NULL) == BIGFLOAT_ERR_INVALID_E);

    /* 部分消费 */
    CHECK(bigfloat_from_str(&v, "12.5abc", &ctx, &end) == BIGFLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'a');
    end = NULL;
    CHECK(bigfloat_from_str(&v, "-3e2x", &ctx, &end) == BIGFLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'x');
    /* 指数部分无数字不消费 */
    end = NULL;
    CHECK(bigfloat_from_str(&v, "7e", &ctx, &end) == BIGFLOAT_OK_E);
    CHECK(end != NULL && end[0] == 'e');

    /* + 号解析 */
    CHECK(parse(&v, "+5", &ctx) == 0);
    double out = 0.0;
    bigfloat_to_f64(&v, &out);
    CHECK(out == 5.0);

    bigfloat_free(&v);
}

/* ------------------------------------------------------------------ */
/* 4. 算术 vs double                                                    */
/* ------------------------------------------------------------------ */

static void test_arith_double(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);

    const double x = 0.1;
    const double y = 0.2;
    bigfloat_from_f64(&a, x);
    bigfloat_from_f64(&b, y);
    double out = 0.0;

    bigfloat_add(&c, &a, &b, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == x + y);
    bigfloat_sub(&c, &a, &b, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == x - y);
    bigfloat_mul(&c, &a, &b, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == x * y);
    bigfloat_div(&c, &a, &b, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == x / y);
    bigfloat_sqrt(&c, &a, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == sqrt(x));

    /* 减法变号：0.3 - 0.1 = 0.2 */
    bigfloat_from_f64(&a, 0.3);
    bigfloat_from_f64(&b, 0.1);
    bigfloat_sub(&c, &a, &b, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == 0.3 - 0.1);

    /* NULL / 别名安全 */
    CHECK(bigfloat_add(NULL, &a, &b, &ctx) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_add(&c, NULL, &b, &ctx) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_add(&c, &a, &b, NULL) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_add(&c, &c, &c, &ctx) == BIGFLOAT_OK_E);  /* 自别名 */

    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */
/* 5. 特殊值传播                                                        */
/* ------------------------------------------------------------------ */

static void test_special(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);

    parse(&a, "inf", &ctx);
    parse(&b, "-inf", &ctx);
    bigfloat_add(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_nan(&c));                  /* +∞ + −∞ = NaN */
    bigfloat_sub(&c, &a, &a, &ctx);
    CHECK(bigfloat_is_nan(&c));                  /* +∞ − +∞ = NaN */
    bigfloat_add(&c, &a, &a, &ctx);
    CHECK(bigfloat_is_inf(&c) && c.flag == BIGFLOAT_POS_INF_E);
    bigfloat_add(&c, &b, &b, &ctx);
    CHECK(c.flag == BIGFLOAT_NEG_INF_E);

    parse(&a, "5", &ctx);
    parse(&b, "0", &ctx);
    bigfloat_mul(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_zero(&c) && c.flag == BIGFLOAT_POS_ZERO_E);
    bigfloat_from_str(&a, "inf", &ctx, NULL);
    bigfloat_mul(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_nan(&c));                  /* ∞ × 0 = NaN */
    bigfloat_div(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_inf(&c) && c.flag == BIGFLOAT_POS_INF_E);  /* ∞/0 */
    bigfloat_div(&c, &b, &b, &ctx);
    CHECK(bigfloat_is_nan(&c));                  /* 0/0 = NaN */
    bigfloat_from_str(&a, "-5", &ctx, NULL);
    bigfloat_div(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGFLOAT_NEG_INF_E);         /* −5/0 = −∞ */
    bigfloat_sqrt(&c, &a, &ctx);
    CHECK(bigfloat_is_nan(&c));                  /* sqrt(−5) = NaN */
    bigfloat_sqrt(&c, &b, &ctx);
    CHECK(bigfloat_is_zero(&c) && c.flag == BIGFLOAT_POS_ZERO_E);
    bigfloat_from_str(&b, "-0", &ctx, NULL);
    bigfloat_sqrt(&c, &b, &ctx);
    CHECK(c.flag == BIGFLOAT_NEG_ZERO_E);        /* sqrt(−0) = −0 */

    /* NaN 吸收 */
    parse(&a, "nan", &ctx);
    parse(&b, "1.5", &ctx);
    bigfloat_add(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_nan(&c));
    bigfloat_mul(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_nan(&c));
    bigfloat_div(&c, &b, &a, &ctx);
    CHECK(bigfloat_is_nan(&c));
    bigfloat_sqrt(&c, &a, &ctx);
    CHECK(bigfloat_is_nan(&c));

    /* ∞/∞、有限/∞ */
    parse(&a, "inf", &ctx);
    parse(&b, "3", &ctx);
    bigfloat_div(&c, &a, &b, &ctx);
    CHECK(bigfloat_is_inf(&c));
    bigfloat_div(&c, &b, &a, &ctx);
    CHECK(bigfloat_is_zero(&c) && c.flag == BIGFLOAT_POS_ZERO_E);

    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* 零符号规则（§11） */
static void test_zero_sign(void) {
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();

    bigfloat_from_str(&a, "-0", &ctx, NULL);
    bigfloat_from_str(&b, "0", &ctx, NULL);
    /* 同号零相加：取该符号 */
    bigfloat_add(&c, &a, &a, &ctx);
    CHECK(c.flag == BIGFLOAT_NEG_ZERO_E);
    bigfloat_add(&c, &b, &b, &ctx);
    CHECK(c.flag == BIGFLOAT_POS_ZERO_E);
    /* 异号零：最近舍入 +0，向 −∞ 舍入 −0 */
    bigfloat_add(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGFLOAT_POS_ZERO_E);
    bigfloat_ctx_ty ctx_neg;
    bigfloat_ctx_make(&ctx_neg, 53, 11, BIGFLOAT_ROUND_TOWARD_NEG_E);
    bigfloat_add(&c, &a, &b, &ctx_neg);
    CHECK(c.flag == BIGFLOAT_NEG_ZERO_E);

    /* x − x = +0（最近）/ −0（向 −∞） */
    bigfloat_from_str(&a, "3.5", &ctx, NULL);
    bigfloat_sub(&c, &a, &a, &ctx);
    CHECK(c.flag == BIGFLOAT_POS_ZERO_E);
    bigfloat_sub(&c, &a, &a, &ctx_neg);
    CHECK(c.flag == BIGFLOAT_NEG_ZERO_E);

    /* 负 × 正 = 负零 */
    bigfloat_from_str(&a, "-0", &ctx, NULL);
    bigfloat_from_str(&b, "2", &ctx, NULL);
    bigfloat_mul(&c, &a, &b, &ctx);
    CHECK(c.flag == BIGFLOAT_NEG_ZERO_E);

    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */
/* 6. 舍入模式与上下溢                                                  */
/* ------------------------------------------------------------------ */

static void test_rounding_modes(void) {
    bigfloat_ctx_ty ctx;
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);

    /* 向零截断：1 + 2^-24 在 24 位下 → 1 */
    bigfloat_ctx_make(&ctx, 24, 8, BIGFLOAT_ROUND_TOWARD_ZERO_E);
    bigfloat_from_str(&a, "1.000000059604644775390625", &ctx, NULL);
    bigfloat_from_str(&b, "1", &ctx, NULL);
    CHECK(bigfloat_eq(&a, &b));

    /* 最近偶舍入（24 位）：1/3 正确舍入 = float 除法结果 */
    bigfloat_ctx_make(&ctx, 24, 8, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_from_str(&a, "1", &ctx, NULL);
    bigfloat_from_str(&b, "3", &ctx, NULL);
    bigfloat_div(&c, &a, &b, &ctx);
    double out = 0.0;
    bigfloat_to_f64(&c, &out);
    CHECK(out == (double)(1.0f / 3.0f));

    /* ties-to-even：2.5 + 2^-23 与 2.5 / 2.5+2^-22 等距 → 取尾数偶的 2.5 */
    bigfloat_ctx_make(&ctx, 24, 8, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_from_str(&a, "2.50000011920928955078125", &ctx, NULL);
    bigfloat_to_f64(&a, &out);
    CHECK(out == 2.5);
    /* 远离零舍入 → 2.5 + 2^-22 */
    bigfloat_ctx_make(&ctx, 24, 8, BIGFLOAT_ROUND_AWAY_ZERO_E);
    bigfloat_from_str(&a, "2.50000011920928955078125", &ctx, NULL);
    bigfloat_to_f64(&a, &out);
    CHECK(out == 2.5000002384185791015625);

    /* 上溢：最近舍入 → ±∞；向零 → 最大有限值 (2^24−1)×2^7 */
    bigfloat_ctx_make(&ctx, 24, 4, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    /* emax = 2^3 − 1 = 7 → max = (2^24−1) × 2^7 ≈ 2.14748352e9 */
    bigfloat_from_str(&a, "1e10", &ctx, NULL);
    CHECK(bigfloat_is_inf(&a));  /* 1e10 > 2^31 → 上溢 */
    bigfloat_ctx_make(&ctx, 24, 4, BIGFLOAT_ROUND_TOWARD_ZERO_E);
    bigfloat_from_str(&a, "1e10", &ctx, NULL);
    CHECK(bigfloat_is_normal(&a));
    bigfloat_to_f64(&a, &out);
    CHECK(out == 2147483520.0);  /* (2^24−1)×2^7 */
    char buf[64];
    CHECK(bigfloat_to_str(&a, 0, buf, sizeof(buf), NULL) == BIGFLOAT_OK_E);
    bigfloat_ty back;
    bigfloat_init(&back);
    /* to_str 的往返保证是最近偶舍入下的（其验证 ctx 用最近偶），
     * 回解析须用最近偶 ctx，不能用向零 ctx */
    bigfloat_ctx_ty ctx_nearest;
    bigfloat_ctx_make(&ctx_nearest, 24, 4, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    CHECK(bigfloat_from_str(&back, buf, &ctx_nearest, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_eq(&back, &a));
    bigfloat_free(&back);

    /* 下溢：flush-to-zero，符号按舍入方向 */
    bigfloat_ctx_make(&ctx, 24, 4, BIGFLOAT_ROUND_TOWARD_NEG_E);
    bigfloat_from_str(&a, "-1e-20", &ctx, NULL);  /* < 2^-27ish → flush */
    CHECK(bigfloat_is_zero(&a) && a.flag == BIGFLOAT_NEG_ZERO_E);

    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */
/* 7. sqrt                                                              */
/* ------------------------------------------------------------------ */

static void test_sqrt(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&c);

    bigfloat_from_str(&a, "4", &ctx, NULL);
    bigfloat_sqrt(&c, &a, &ctx);
    double out = 0.0;
    bigfloat_to_f64(&c, &out);
    CHECK(out == 2.0);

    bigfloat_from_str(&a, "2", &ctx, NULL);
    bigfloat_sqrt(&c, &a, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == sqrt(2.0));

    bigfloat_from_str(&a, "0.1", &ctx, NULL);
    bigfloat_sqrt(&c, &a, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == sqrt(0.1));

    bigfloat_from_str(&a, "1e300", &ctx, NULL);
    bigfloat_sqrt(&c, &a, &ctx);
    bigfloat_to_f64(&c, &out);
    CHECK(out == sqrt(1e300));

    /* 完全平方数精确 */
    bigfloat_from_str(&a, "15241578750190521", &ctx, NULL);  /* 123456789² */
    bigfloat_sqrt(&c, &a, &ctx);
    bigfloat_ty b;
    bigfloat_init(&b);
    bigfloat_from_str(&b, "123456789", &ctx, NULL);
    CHECK(bigfloat_eq(&c, &b));
    bigfloat_free(&b);

    /* 128 位：sqrt(2)² = 2 − 1 ulp（精确整数运算验证） */
    bigfloat_ctx_ty ctx128;
    bigfloat_ctx_make(&ctx128, 113, 15, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_ty d;
    bigfloat_init(&d);
    bigfloat_from_str(&a, "2", &ctx128, NULL);
    bigfloat_sqrt(&c, &a, &ctx128);
    bigfloat_mul(&d, &c, &c, &ctx128);
    bigfloat_ty e;
    bigfloat_init(&e);
    bigfloat_from_str(&e, "1.9999999999999999999999999999999998", &ctx128, NULL);
    CHECK(bigfloat_eq(&d, &e));
    bigfloat_free(&e);
    bigfloat_free(&d);

    bigfloat_free(&c);
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */
/* 8. 比较                                                              */
/* ------------------------------------------------------------------ */

static void test_cmp(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_init(&a);
    bigfloat_init(&b);

    /* 跨指数比较（历史缺陷：cmp_mag_exp 方向反） */
    bigfloat_from_str(&a, "1", &ctx, NULL);       /* 2^0 */
    bigfloat_from_str(&b, "0.5", &ctx, NULL);     /* 2^-1 */
    CHECK(bigfloat_cmp(&a, &b) > 0);
    CHECK(bigfloat_cmp(&b, &a) < 0);
    bigfloat_from_str(&a, "1e300", &ctx, NULL);
    bigfloat_from_str(&b, "1e-300", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) > 0);
    bigfloat_from_str(&a, "-1e300", &ctx, NULL);
    bigfloat_from_str(&b, "1e-300", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) < 0);              /* 负数小于正数 */
    bigfloat_from_str(&a, "-1e300", &ctx, NULL);
    bigfloat_from_str(&b, "-1e-300", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) < 0);              /* −大 < −小 */
    bigfloat_from_str(&a, "1.0000000000000002", &ctx, NULL);
    bigfloat_from_str(&b, "1", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) > 0);              /* 尾数仅差 1 ulp */

    /* 全序：−∞ < 负 < ±0 < 正 < +∞；NaN 返回 2 */
    bigfloat_from_str(&a, "-inf", &ctx, NULL);
    bigfloat_from_str(&b, "inf", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) < 0);
    bigfloat_from_str(&a, "0", &ctx, NULL);
    bigfloat_from_str(&b, "-0", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) == 0);
    CHECK(bigfloat_eq(&a, &b));
    bigfloat_from_str(&a, "nan", &ctx, NULL);
    bigfloat_from_str(&b, "1", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) == 2);
    CHECK(bigfloat_cmp(&a, &a) == 2);
    CHECK(!bigfloat_eq(&a, &a));                  /* NaN ≠ NaN */
    bigfloat_from_str(&a, "-1", &ctx, NULL);
    bigfloat_from_str(&b, "0", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) < 0);

    /* 幅值相同符号相反 */
    bigfloat_from_str(&a, "3.5", &ctx, NULL);
    bigfloat_from_str(&b, "-3.5", &ctx, NULL);
    CHECK(bigfloat_cmp(&a, &b) > 0);

    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */
/* 9. decompose / compose / neg / from_bigint                          */
/* ------------------------------------------------------------------ */

static void test_decompose_compose(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty v;
    bigfloat_ty w;
    bigint_bin_ty mant;
    bigint_bin_init(&mant);
    bigfloat_init(&v);
    bigfloat_init(&w);

    bigfloat_from_str(&v, "-123.456", &ctx, NULL);
    int64_t exp = 0;
    bigfloat_flag_ty flag = BIGFLOAT_NAN_E;
    CHECK(bigfloat_decompose(&v, &mant, &exp, &flag) == BIGFLOAT_OK_E);
    CHECK(flag == BIGFLOAT_NEG_E);
    CHECK(bigint_bin_bit_len(&mant) == 53U);
    CHECK(bigfloat_compose(&w, &mant, exp, flag, &ctx) == BIGFLOAT_OK_E);
    CHECK(bigfloat_eq(&w, &v));
    /* 任意尾数合成 → 按 ctx 舍入规范化 */
    bigint_bin_ty big;
    bigint_bin_init(&big);
    bigint_bin_from_u64(&big, 3);
    bigint_bin_shl(&big, &big, 60);  /* 3×2^60（62 位）→ 舍入到 53 位 */
    CHECK(bigfloat_compose(&w, &big, 0, BIGFLOAT_POS_E, &ctx) == BIGFLOAT_OK_E);
    check_invariants("compose", &w);
    /* 特殊值合成忽略 mant/exp */
    CHECK(bigfloat_compose(&w, &big, 123, BIGFLOAT_NAN_E, &ctx)
            == BIGFLOAT_OK_E);
    CHECK(bigfloat_is_nan(&w));
    /* NULL / 非法 flag */
    CHECK(bigfloat_decompose(NULL, &mant, &exp, &flag) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_decompose(&v, NULL, &exp, &flag) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_compose(NULL, &big, 0, BIGFLOAT_POS_E, &ctx)
            == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_compose(&w, NULL, 0, BIGFLOAT_POS_E, &ctx)
            == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_compose(&w, &big, 0, (bigfloat_flag_ty)77, &ctx)
            == BIGFLOAT_ERR_INVALID_E);

    bigint_bin_free(&big);
    bigint_bin_free(&mant);
    bigfloat_free(&w);
    bigfloat_free(&v);
}

static void test_neg_from_bigint(void) {
    bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty v;
    bigfloat_init(&v);
    bigint_bin_ty n;
    bigint_bin_init(&n);

    bigfloat_from_str(&v, "2.5", &ctx, NULL);
    CHECK(bigfloat_neg(&v) == BIGFLOAT_OK_E);
    CHECK(v.flag == BIGFLOAT_NEG_E);
    CHECK(bigfloat_neg(&v) == BIGFLOAT_OK_E);
    CHECK(v.flag == BIGFLOAT_POS_E);
    bigfloat_from_str(&v, "-0", &ctx, NULL);
    bigfloat_neg(&v);
    CHECK(v.flag == BIGFLOAT_POS_ZERO_E);
    bigfloat_from_str(&v, "inf", &ctx, NULL);
    bigfloat_neg(&v);
    CHECK(v.flag == BIGFLOAT_NEG_INF_E);
    bigfloat_from_str(&v, "nan", &ctx, NULL);
    bigfloat_neg(&v);
    CHECK(bigfloat_is_nan(&v));
    CHECK(bigfloat_neg(NULL) == BIGFLOAT_ERR_INVALID_E);

    /* from_bigint：精确与舍入 */
    bigint_bin_from_u64(&n, 123456789);
    CHECK(bigfloat_from_bigint(&v, &n, &ctx) == BIGFLOAT_OK_E);
    double out = 0.0;
    bigfloat_to_f64(&v, &out);
    CHECK(out == 123456789.0);
    /* 大整数（超过 53 位）→ 按 ctx 舍入 */
    bigint_bin_ty big;
    bigint_bin_init(&big);
    bigint_bin_from_u64(&big, 1);
    bigint_bin_shl(&big, &big, 60);  /* 2^60 */
    bigint_bin_add(&big, &big, &big);  /* 2^61 */
    CHECK(bigfloat_from_bigint(&v, &big, &ctx) == BIGFLOAT_OK_E);
    check_invariants("from_bigint", &v);
    bigint_bin_free(&big);

    bigint_bin_free(&n);
    bigfloat_free(&v);
}

/* ------------------------------------------------------------------ */
/* 10. 极小上下文边界                                                    */
/* ------------------------------------------------------------------ */

static void test_min_ctx(void) {
    bigfloat_ctx_ty ctx;
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);

    /* mant_bits = 2：只有 1.0、1.5、2.0（2 位尾数）可表示 */
    bigfloat_ctx_make(&ctx, 2, 2, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    CHECK(bigfloat_from_str(&a, "1", &ctx, NULL) == BIGFLOAT_OK_E);
    CHECK(bigfloat_from_str(&b, "3", &ctx, NULL) == BIGFLOAT_OK_E);
    /* 3 = 1.5×2^1 → mant=3(2位), exp=0 */
    expect_value("min ctx 3", &b, BIGFLOAT_POS_E, "3", 0);
    bigfloat_mul(&c, &a, &b, &ctx);
    /* 1×3=3 精确 */
    CHECK(bigfloat_eq(&c, &b));
    bigfloat_from_str(&a, "4", &ctx, NULL);
    bigfloat_div(&c, &a, &b, &ctx);  /* 4/3 ≈ 1.333 → 2 位 → 1.5 */
    expect_value("min ctx 4/3", &c, BIGFLOAT_POS_E, "3", -1);
    /* exp_bits = 1：emin = −1，emax = 0 */
    bigfloat_ctx_make(&ctx, 2, 1, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigfloat_from_str(&a, "1", &ctx, NULL);   /* 1 = 2×2^-1，在范围内 */
    CHECK(bigfloat_is_normal(&a));
    bigfloat_from_str(&a, "0.5", &ctx, NULL); /* 0.5 = 2×2^-2 < emin → flush */
    CHECK(bigfloat_is_zero(&a));
    bigfloat_from_str(&a, "4", &ctx, NULL);   /* 2^2 > emax=0 → 上溢 */
    CHECK(bigfloat_is_inf(&a));

    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */

int main(void) {
    test_lifecycle();
    test_classify();
    test_from_to_f64();
    test_str_roundtrip();
    test_parse_errors();
    test_arith_double();
    test_special();
    test_zero_sign();
    test_rounding_modes();
    test_sqrt();
    test_cmp();
    test_decompose_compose();
    test_neg_from_bigint();
    test_min_ctx();

    if (g_fail == 0) {
        printf("test_bigfloat: ALL PASS\n");
        return 0;
    }
    printf("test_bigfloat: %d FAILURES\n", g_fail);
    return 1;
}
