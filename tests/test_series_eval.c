/*
 * test_series_eval.c：路线 A 二项式级数自适应求值（TODO §C2）。
 *
 * 覆盖模型 (A,B)、自适应阶、bigfloat/bigdecimal 求值（含与精确有理数舍入
 * 结果的对照）、错误路径。
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

static void test_model(void) {
    bigfrac_ty a, A, B;
    bigfrac_init(&a);
    bigfrac_init(&A);
    bigfrac_init(&B);

    bigfrac_from_str(&a, "1/2", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&A, "1") && frac_eq_str(&B, "1"));

    bigfrac_from_str(&a, "-1", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&A, "1"));

    bigfrac_from_str(&a, "5/2", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&A, "5/2"));

    bigfrac_from_str(&a, "-2", NULL);
    CHECK(nex_series_binom_model(&A, &B, &a) == BIGFRAC_ERR_UNSUPPORTED_E);

    bigfrac_free(&B);
    bigfrac_free(&A);
    bigfrac_free(&a);
}

static void test_order(void) {
    bigfrac_ty a, r, eps;
    bigfrac_init(&a);
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
    bigfrac_free(&a);
}

static void test_eval_float(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfrac_ty alpha, eps;
    bigfloat_ty z, out, want;
    bigfrac_init(&alpha);
    bigfrac_init(&eps);
    bigfloat_init(&z);
    bigfloat_init(&out);
    bigfloat_init(&want);

    bigfrac_from_str(&eps, "1/100000000000000000000", NULL);   /* 1e-20 */

    /* (1 + 1/2)^2 = 9/4 = 2.25（精确） */
    bigfrac_from_str(&alpha, "2", NULL);
    bigfloat_from_f64(&z, 0.5);
    CHECK(nex_series_binom_eval_float(&out, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    bigfloat_from_f64(&want, 2.25);
    CHECK(bigfloat_eq(&out, &want));

    /* (1 + 1/2)^{-1} = 2/3；与精确有理数一次舍入对照 */
    bigfrac_from_str(&alpha, "-1", NULL);
    bigfloat_from_f64(&z, 0.5);
    CHECK(nex_series_binom_eval_float(&out, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_OK_E);
    bigfrac_ty two_thirds;
    bigfrac_init(&two_thirds);
    bigfrac_from_str(&two_thirds, "2/3", NULL);
    CHECK(nex_convert_frac_to_float(&want, &two_thirds, &ctx) ==
          NEX_CONVERT_OK_E);
    CHECK(bigfloat_eq(&out, &want));
    bigfrac_free(&two_thirds);

    /* alpha < -1 -> INVALID */
    bigfrac_from_str(&alpha, "-2", NULL);
    bigfloat_from_f64(&z, 0.5);
    CHECK(nex_series_binom_eval_float(&out, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_ERR_INVALID_E);

    /* |z| >= 1 -> INVALID */
    bigfrac_from_str(&alpha, "1/2", NULL);
    bigfloat_from_f64(&z, 1.0);
    CHECK(nex_series_binom_eval_float(&out, &alpha, &z, &eps, &ctx) ==
          BIGFLOAT_ERR_INVALID_E);

    bigfloat_free(&want);
    bigfloat_free(&out);
    bigfloat_free(&z);
    bigfrac_free(&eps);
    bigfrac_free(&alpha);
}

static void test_eval_decimal(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigfrac_ty alpha, eps, two_thirds;
    bigdecimal_ty z, out, want;
    bigfrac_init(&alpha);
    bigfrac_init(&eps);
    bigfrac_init(&two_thirds);
    bigdecimal_init(&z);
    bigdecimal_init(&out);
    bigdecimal_init(&want);

    bigfrac_from_str(&eps, "1/1000000000000000000000000000000", NULL); /* 1e-30 */

    /* (1 + 0.5)^{-1} = 2/3；与精确有理数一次舍入对照 */
    bigfrac_from_str(&alpha, "-1", NULL);
    bigdecimal_from_str(&z, "0.5", &ctx, NULL);
    CHECK(nex_series_binom_eval_decimal(&out, &alpha, &z, &eps, &ctx) ==
          BIGDECIMAL_OK_E);
    bigfrac_from_str(&two_thirds, "2/3", NULL);
    CHECK(nex_convert_frac_to_decimal(&want, &two_thirds, &ctx) ==
          NEX_CONVERT_OK_E);
    CHECK(bigdecimal_eq(&out, &want));

    bigdecimal_free(&want);
    bigdecimal_free(&out);
    bigdecimal_free(&z);
    bigfrac_free(&two_thirds);
    bigfrac_free(&eps);
    bigfrac_free(&alpha);
}

int main(void) {
    test_model();
    test_order();
    test_eval_float();
    test_eval_decimal();

    if (failures == 0) {
        printf("test_series_eval: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
