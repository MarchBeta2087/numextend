/*
 * test_series_eval.c：路线 A 幂级数自适应求值（TODO §C2）。
 *
 * 覆盖：二项式模型/阶、bigfloat/bigdecimal 求值（含误差输出）、
 * exp/cos/sin 的比值型尾界求值（含十进制已知值对照）。
 */

#include <stdio.h>

#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/convert/nex_convert.h"
#include "nex/series/nex_series.h"

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static int frac_eq_str(const bigfrac_ty *f, const char *s) {
    bigfrac_ty e;
    bigfrac_init(&e);
    bigfrac_from_str(&e, s, NULL);
    int eq = (bigfrac_cmp(f, &e) == 0);
    bigfrac_free(&e);
    return eq;
}

static int dec_eq_str(const bigdecimal_ty *v, const char *s,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty e;
    bigdecimal_init(&e);
    bigdecimal_from_str(&e, s, ctx, NULL);
    int eq = bigdecimal_eq(v, &e);
    bigdecimal_free(&e);
    return eq;
}

static void test_binom_model_order(void) {
    bigfrac_ty a, A, B;
    bigfrac_init(&a);
    bigfrac_init(&A);
    bigfrac_init(&B);

    bigfrac_from_str(&a, "1/2", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&A, "1") && frac_eq_str(&B, "1"));

    bigfrac_from_str(&a, "5/2", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&A, "5/2"));

    bigfrac_from_str(&a, "-2", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_ERR_UNSUPPORTED_E);

    bigfrac_ty r, eps;
    bigfrac_init(&r);
    bigfrac_init(&eps);
    bigfrac_from_str(&a, "1/2", NULL);
    bigfrac_from_str(&r, "1/2", NULL);
    bigfrac_from_str(&eps, "1/1000", NULL);
    size_t n = 0U;
    CHECK(nex_series_binom_order(&n, &a, &r, &eps) == BIGFRAC_OK_E);
    CHECK(n == 11U);
    bigfrac_free(&eps);
    bigfrac_free(&r);

    bigfrac_free(&B);
    bigfrac_free(&A);
    bigfrac_free(&a);
}

static void test_fact_orders(void) {
    bigfrac_ty r, eps;
    bigfrac_init(&r);
    bigfrac_init(&eps);
    bigfrac_from_str(&r, "0", NULL);
    bigfrac_from_str(&eps, "1/1000", NULL);

    size_t n = 0U;
    CHECK(nex_series_exp_order(&n, &r, &eps) == BIGFRAC_OK_E && n == 1U);
    CHECK(nex_series_cos_order(&n, &r, &eps) == BIGFRAC_OK_E && n == 1U);
    CHECK(nex_series_sin_order(&n, &r, &eps) == BIGFRAC_OK_E && n == 0U);

    /* r=1：exp 需 ~log2(1/eps) 量级；只检查有界且单调 */
    bigfrac_from_str(&r, "1", NULL);
    CHECK(nex_series_exp_order(&n, &r, &eps) == BIGFRAC_OK_E && n >= 2U);

    bigfrac_free(&eps);
    bigfrac_free(&r);
}

static void test_binom_eval_float(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfrac_ty alpha, eps;
    bigfloat_ty z, out, err, want;
    bigfrac_init(&alpha);
    bigfrac_init(&eps);
    bigfloat_init(&z);
    bigfloat_init(&out);
    bigfloat_init(&err);
    bigfloat_init(&want);

    bigfrac_from_str(&eps, "1/100000000000000000000", NULL);   /* 1e-20 */

    /* (1 + 1/2)^2 = 2.25（精确），并检查误差界非负 */
    bigfrac_from_str(&alpha, "2", NULL);
    bigfloat_from_f64(&z, 0.5);
    CHECK(nex_series_binom_eval_float(&out, &err, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    bigfloat_from_f64(&want, 2.25);
    CHECK(bigfloat_eq(&out, &want));
    double ed = -1.0;
    CHECK(bigfloat_to_f64(&err, &ed) == BIGFLOAT_OK_E && ed >= 0.0);

    /* (1 + 1/2)^{-1} = 2/3；与精确有理数一次舍入对照 */
    bigfrac_from_str(&alpha, "-1", NULL);
    bigfloat_from_f64(&z, 0.5);
    CHECK(nex_series_binom_eval_float(&out, NULL, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    bigfrac_ty two_thirds;
    bigfrac_init(&two_thirds);
    bigfrac_from_str(&two_thirds, "2/3", NULL);
    CHECK(nex_convert_frac_to_float(&want, &two_thirds, &ctx) ==
          NEX_CONVERT_OK_E);
    CHECK(bigfloat_eq(&out, &want));
    bigfrac_free(&two_thirds);

    /* alpha < -1 / |z| >= 1 -> INVALID */
    bigfrac_from_str(&alpha, "-2", NULL);
    bigfloat_from_f64(&z, 0.5);
    CHECK(nex_series_binom_eval_float(&out, NULL, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_ERR_INVALID_E);
    bigfrac_from_str(&alpha, "1/2", NULL);
    bigfloat_from_f64(&z, 1.0);
    CHECK(nex_series_binom_eval_float(&out, NULL, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_ERR_INVALID_E);

    bigfloat_free(&want);
    bigfloat_free(&err);
    bigfloat_free(&out);
    bigfloat_free(&z);
    bigfrac_free(&eps);
    bigfrac_free(&alpha);
}

static void test_fact_eval_float(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfrac_ty eps;
    bigfloat_ty z, out, err, want;
    bigfrac_init(&eps);
    bigfloat_init(&z);
    bigfloat_init(&out);
    bigfloat_init(&err);
    bigfloat_init(&want);
    bigfrac_from_str(&eps, "1/100000000000000000000", NULL);

    /* z = 0：exp=1, cos=1, sin=0 */
    bigfloat_from_f64(&z, 0.0);
    CHECK(nex_series_exp_eval_float(&out, &err, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    bigfloat_from_f64(&want, 1.0);
    CHECK(bigfloat_eq(&out, &want));
    double ed = -1.0;
    CHECK(bigfloat_to_f64(&err, &ed) == BIGFLOAT_OK_E && ed >= 0.0);

    CHECK(nex_series_cos_eval_float(&out, NULL, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    CHECK(bigfloat_eq(&out, &want));

    CHECK(nex_series_sin_eval_float(&out, NULL, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    bigfloat_from_f64(&want, 0.0);
    CHECK(bigfloat_eq(&out, &want));

    bigfloat_free(&want);
    bigfloat_free(&err);
    bigfloat_free(&out);
    bigfloat_free(&z);
    bigfrac_free(&eps);
}

static void test_fact_eval_decimal(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigfrac_ty eps;
    bigdecimal_ty z, out;
    bigfrac_init(&eps);
    bigdecimal_init(&z);
    bigdecimal_init(&out);
    bigfrac_from_str(&eps, "1/1000000000000000000000000000000", NULL); /* 1e-30 */

    bigdecimal_from_str(&z, "1", &ctx, NULL);

    CHECK(nex_series_exp_eval_decimal(&out, NULL, &z, &eps, &ctx) ==
          BIGDECIMAL_OK_E);
    CHECK(dec_eq_str(&out, "2.718281828459045", &ctx));

    CHECK(nex_series_cos_eval_decimal(&out, NULL, &z, &eps, &ctx) ==
          BIGDECIMAL_OK_E);
    CHECK(dec_eq_str(&out, "0.5403023058681397", &ctx));

    CHECK(nex_series_sin_eval_decimal(&out, NULL, &z, &eps, &ctx) ==
          BIGDECIMAL_OK_E);
    CHECK(dec_eq_str(&out, "0.8414709848078965", &ctx));

    bigdecimal_free(&out);
    bigdecimal_free(&z);
    bigfrac_free(&eps);
}

int main(void) {
    test_binom_model_order();
    test_fact_orders();
    test_binom_eval_float();
    test_fact_eval_float();
    test_fact_eval_decimal();

    if (failures == 0) {
        printf("test_series_eval: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
