/*
 * test_ball.c：bigfloat 球式（中心 + 半径）严格区间算术（TODO §C2 路线 B）。
 *
 * 覆盖 set/contains、加/减/乘/除的区间传播、除零区间与非法参数。
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "nex/ball/nex_ball.h"
#include "nex/bigfloat/nex_bigfloat.h"

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static void mk(bigfloat_ball_ty *b, double mid, double rad,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ty m;
    bigfloat_ty r;
    bigfloat_init(&m);
    bigfloat_init(&r);
    bigfloat_from_f64(&m, mid);
    bigfloat_from_f64(&r, rad);
    (void)bigfloat_ball_set(b, &m, &r, ctx);
    bigfloat_free(&r);
    bigfloat_free(&m);
}

static int contains_d(const bigfloat_ball_ty *b, double x,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ty t;
    bigfloat_init(&t);
    bigfloat_from_f64(&t, x);
    int result = bigfloat_ball_contains(b, &t, ctx);
    bigfloat_free(&t);
    return result;
}

static void mk_dec(bigdecimal_ball_ty *b, const char *mid, const char *rad,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty m;
    bigdecimal_ty r;
    bigdecimal_init(&m);
    bigdecimal_init(&r);
    bigdecimal_from_str(&m, mid, ctx, NULL);
    bigdecimal_from_str(&r, rad, ctx, NULL);
    (void)bigdecimal_ball_set(b, &m, &r, ctx);
    bigdecimal_free(&r);
    bigdecimal_free(&m);
}

static int contains_dec(const bigdecimal_ball_ty *b, const char *x,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty t;
    bigdecimal_init(&t);
    bigdecimal_from_str(&t, x, ctx, NULL);
    int result = bigdecimal_ball_contains(b, &t, ctx);
    bigdecimal_free(&t);
    return result;
}

static void test_decimal_ball(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ball_ty a;
    bigdecimal_ball_ty b;
    bigdecimal_ball_ty r;
    bigdecimal_ball_init(&a);
    bigdecimal_ball_init(&b);
    bigdecimal_ball_init(&r);

    mk_dec(&a, "1", "0", &ctx);
    mk_dec(&b, "2", "0", &ctx);
    CHECK(bigdecimal_ball_add(&r, &a, &b, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "3", &ctx));
    CHECK(!contains_dec(&r, "3.0001", &ctx));

    mk_dec(&a, "1.25", "0.25", &ctx);
    mk_dec(&b, "0.75", "0.25", &ctx);
    CHECK(bigdecimal_ball_add(&r, &a, &b, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "1.5", &ctx));
    CHECK(contains_dec(&r, "2.5", &ctx));
    CHECK(!contains_dec(&r, "2.6", &ctx));

    mk_dec(&a, "1.5", "0.5", &ctx);
    mk_dec(&b, "3.5", "0.5", &ctx);
    CHECK(bigdecimal_ball_mul(&r, &a, &b, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "3", &ctx));
    CHECK(contains_dec(&r, "8", &ctx));
    CHECK(!contains_dec(&r, "8.1", &ctx));

    mk_dec(&a, "7", "1", &ctx);
    mk_dec(&b, "3", "1", &ctx);
    CHECK(bigdecimal_ball_div(&r, &a, &b, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "1.5", &ctx));
    CHECK(contains_dec(&r, "4", &ctx));
    CHECK(!contains_dec(&r, "4.1", &ctx));

    mk_dec(&b, "0", "1", &ctx);
    CHECK(bigdecimal_ball_div(&r, &a, &b, &ctx) == BIGDECIMAL_ERR_INVALID_E);

    bigdecimal_ball_free(&r);
    bigdecimal_ball_free(&b);
    bigdecimal_ball_free(&a);
}

static double center_d(const bigfloat_ball_ty *b) {
    double d = 0.0;
    bigfloat_to_f64(&b->mid, &d);
    return d;
}

static void test_elem(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ball_ty x;
    bigfloat_ball_ty r;
    bigfloat_ball_init(&x);
    bigfloat_ball_init(&r);

    /* exp(0) = 1 */
    mk(&x, 0.0, 0.0, &ctx);
    CHECK(bigfloat_ball_exp(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(center_d(&r) == 1.0);

    /* exp(1) ≈ e（实参规约 + 球平方） */
    mk(&x, 1.0, 0.0, &ctx);
    CHECK(bigfloat_ball_exp(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(fabs(center_d(&r) - 2.718281828459045) < 1e-12);

    /* exp(2) ≈ e^2 */
    mk(&x, 2.0, 0.0, &ctx);
    CHECK(bigfloat_ball_exp(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(fabs(center_d(&r) - 7.38905609893065) < 1e-12);

    /* sin(0)=0, cos(0)=1 */
    mk(&x, 0.0, 0.0, &ctx);
    CHECK(bigfloat_ball_sin(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(center_d(&r) == 0.0);
    CHECK(bigfloat_ball_cos(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(center_d(&r) == 1.0);

    /* sin(1), cos(1)（half-angle 倍角） */
    mk(&x, 1.0, 0.0, &ctx);
    CHECK(bigfloat_ball_sin(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(fabs(center_d(&r) - 0.8414709848078965) < 1e-12);
    CHECK(bigfloat_ball_cos(&r, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(fabs(center_d(&r) - 0.5403023058681397) < 1e-12);

    /* |x| 过大（k > 20）-> INVALID */
    mk(&x, 1e9, 0.0, &ctx);
    CHECK(bigfloat_ball_exp(&r, &x, &ctx) == BIGFLOAT_ERR_INVALID_E);

    bigfloat_ball_free(&r);
    bigfloat_ball_free(&x);
}

static double dec_center_d(const bigdecimal_ball_ty *b) {
    char buf[64];
    size_t need = 0U;
    bigdecimal_to_str(&b->mid, BIGDECIMAL_FMT_FIXED_E, buf, sizeof(buf),
                      &need);
    return strtod(buf, NULL);
}

static void test_elem_decimal(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ball_ty x;
    bigdecimal_ball_ty r;
    bigdecimal_ball_init(&x);
    bigdecimal_ball_init(&r);

    mk_dec(&x, "0", "0", &ctx);
    CHECK(bigdecimal_ball_exp(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(dec_center_d(&r) == 1.0);

    mk_dec(&x, "1", "0", &ctx);
    CHECK(bigdecimal_ball_exp(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(fabs(dec_center_d(&r) - 2.718281828459045) < 1e-12);

    mk_dec(&x, "1", "0", &ctx);
    CHECK(bigdecimal_ball_sin(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(fabs(dec_center_d(&r) - 0.8414709848078965) < 1e-12);
    CHECK(bigdecimal_ball_cos(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(fabs(dec_center_d(&r) - 0.5403023058681397) < 1e-12);

    mk_dec(&x, "1000000000", "0", &ctx);
    CHECK(bigdecimal_ball_exp(&r, &x, &ctx) == BIGDECIMAL_ERR_INVALID_E);

    /* sqrt / atan：球应包含真值（decimal64） */
    mk_dec(&x, "2", "0", &ctx);
    CHECK(bigdecimal_ball_sqrt(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "1.414213562373095", &ctx));
    mk_dec(&x, "1", "0", &ctx);
    CHECK(bigdecimal_ball_atan(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "0.7853981633974483", &ctx));
    mk_dec(&x, "2", "0", &ctx);
    CHECK(bigdecimal_ball_atan(&r, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&r, "1.107148717794091", &ctx));
    /* 区间含负值的 sqrt 非法 */
    mk_dec(&x, "-1", "0.5", &ctx);
    CHECK(bigdecimal_ball_sqrt(&r, &x, &ctx) == BIGDECIMAL_ERR_INVALID_E);

    bigdecimal_ball_free(&r);
    bigdecimal_ball_free(&x);
}

static void mk_bser(bigfloat_ball_series_ty *s, int64_t val,
        const double *cs, size_t n, const bigfloat_ctx_ty *ctx) {
    bigfloat_ball_ty tmp[8];
    for (size_t i = 0U; i < n; i++) {
        bigfloat_ball_init(&tmp[i]);
        mk(&tmp[i], cs[i], 0.0, ctx);
    }
    (void)bigfloat_ball_series_from_array(s, val, tmp, n);
    for (size_t i = 0U; i < n; i++) {
        bigfloat_ball_free(&tmp[i]);
    }
}

static void test_ball_series(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ball_series_ty a;
    bigfloat_ball_series_ty b;
    bigfloat_ball_series_ty r;
    bigfloat_ball_series_init(&a);
    bigfloat_ball_series_init(&b);
    bigfloat_ball_series_init(&r);
    const double f[] = {1.0, 1.0, 1.0};   /* 1 + z + z^2 */
    mk_bser(&a, 0, f, 3U, &ctx);

    /* f + f = 2 + 2z + 2z^2 */
    CHECK(bigfloat_ball_series_add(&r, &a, &a, 8U, &ctx) == BIGFLOAT_OK_E);
    CHECK(r.val == 0 && r.len == 3U);
    CHECK(contains_d(&r.coeffs[0], 2.0, &ctx));
    CHECK(contains_d(&r.coeffs[1], 2.0, &ctx));
    CHECK(contains_d(&r.coeffs[2], 2.0, &ctx));

    /* f * f = 1 + 2z + 3z^2 + 2z^3 + z^4 */
    CHECK(bigfloat_ball_series_mul(&r, &a, &a, 8U, &ctx) == BIGFLOAT_OK_E);
    CHECK(r.val == 0 && r.len == 5U);
    CHECK(contains_d(&r.coeffs[0], 1.0, &ctx));
    CHECK(contains_d(&r.coeffs[1], 2.0, &ctx));
    CHECK(contains_d(&r.coeffs[2], 3.0, &ctx));
    CHECK(contains_d(&r.coeffs[3], 2.0, &ctx));
    CHECK(contains_d(&r.coeffs[4], 1.0, &ctx));

    /* 求值 f(0.5) = 1.75（球） */
    bigfloat_ball_ty x;
    bigfloat_ball_ty out;
    bigfloat_ball_init(&x);
    bigfloat_ball_init(&out);
    mk(&x, 0.5, 0.0, &ctx);
    CHECK(bigfloat_ball_series_eval(&out, &a, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(contains_d(&out, 1.75, &ctx));

    /* 复合 f(1+z) = 3 + 3z + z^2 */
    const double g[] = {1.0, 1.0};
    mk_bser(&b, 0, g, 2U, &ctx);
    CHECK(bigfloat_ball_series_compose(&r, &a, &b, 8U, &ctx) ==
          BIGFLOAT_OK_E);
    CHECK(r.val == 0 && r.len == 3U);
    CHECK(contains_d(&r.coeffs[0], 3.0, &ctx));
    CHECK(contains_d(&r.coeffs[1], 3.0, &ctx));
    CHECK(contains_d(&r.coeffs[2], 1.0, &ctx));

    bigfloat_ball_free(&out);
    bigfloat_ball_free(&x);
    bigfloat_ball_series_free(&r);
    bigfloat_ball_series_free(&b);
    bigfloat_ball_series_free(&a);
}

static void mk_dbser(bigdecimal_ball_series_ty *s, int64_t val,
        const char *const *cs, size_t n, const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ball_ty tmp[8];
    for (size_t i = 0U; i < n; i++) {
        bigdecimal_ball_init(&tmp[i]);
        mk_dec(&tmp[i], cs[i], "0", ctx);
    }
    (void)bigdecimal_ball_series_from_array(s, val, tmp, n);
    for (size_t i = 0U; i < n; i++) {
        bigdecimal_ball_free(&tmp[i]);
    }
}

static void test_ball_series_decimal(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ball_series_ty a;
    bigdecimal_ball_series_ty b;
    bigdecimal_ball_series_ty r;
    bigdecimal_ball_series_init(&a);
    bigdecimal_ball_series_init(&b);
    bigdecimal_ball_series_init(&r);
    const char *f[] = {"1", "1", "1"};   /* 1 + z + z^2 */
    mk_dbser(&a, 0, f, 3U, &ctx);

    /* f + f = 2 + 2z + 2z^2 */
    CHECK(bigdecimal_ball_series_add(&r, &a, &a, 8U, &ctx) ==
          BIGDECIMAL_OK_E);
    CHECK(r.val == 0 && r.len == 3U);
    CHECK(contains_dec(&r.coeffs[0], "2", &ctx));
    CHECK(contains_dec(&r.coeffs[1], "2", &ctx));
    CHECK(contains_dec(&r.coeffs[2], "2", &ctx));

    /* f * f = 1 + 2z + 3z^2 + 2z^3 + z^4 */
    CHECK(bigdecimal_ball_series_mul(&r, &a, &a, 8U, &ctx) ==
          BIGDECIMAL_OK_E);
    CHECK(r.val == 0 && r.len == 5U);
    CHECK(contains_dec(&r.coeffs[0], "1", &ctx));
    CHECK(contains_dec(&r.coeffs[1], "2", &ctx));
    CHECK(contains_dec(&r.coeffs[2], "3", &ctx));
    CHECK(contains_dec(&r.coeffs[3], "2", &ctx));
    CHECK(contains_dec(&r.coeffs[4], "1", &ctx));

    /* 求值 f(0.5) = 1.75 */
    bigdecimal_ball_ty x;
    bigdecimal_ball_ty out;
    bigdecimal_ball_init(&x);
    bigdecimal_ball_init(&out);
    mk_dec(&x, "0.5", "0", &ctx);
    CHECK(bigdecimal_ball_series_eval(&out, &a, &x, &ctx) == BIGDECIMAL_OK_E);
    CHECK(contains_dec(&out, "1.75", &ctx));

    /* 复合 f(1+z) = 3 + 3z + z^2 */
    const char *g[] = {"1", "1"};
    mk_dbser(&b, 0, g, 2U, &ctx);
    CHECK(bigdecimal_ball_series_compose(&r, &a, &b, 8U, &ctx) ==
          BIGDECIMAL_OK_E);
    CHECK(r.val == 0 && r.len == 3U);
    CHECK(contains_dec(&r.coeffs[0], "3", &ctx));
    CHECK(contains_dec(&r.coeffs[1], "3", &ctx));
    CHECK(contains_dec(&r.coeffs[2], "1", &ctx));

    bigdecimal_ball_free(&out);
    bigdecimal_ball_free(&x);
    bigdecimal_ball_series_free(&r);
    bigdecimal_ball_series_free(&b);
    bigdecimal_ball_series_free(&a);
}

int main(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ball_ty a;
    bigfloat_ball_ty b;
    bigfloat_ball_ty r;
    bigfloat_ball_init(&a);
    bigfloat_ball_init(&b);
    bigfloat_ball_init(&r);

    /* 退化球 [1,1] */
    mk(&a, 1.0, 0.0, &ctx);
    CHECK(contains_d(&a, 1.0, &ctx));
    CHECK(!contains_d(&a, 1.0001, &ctx));

    /* 加法 [1,1]+[2,2]=[3,3] */
    mk(&b, 2.0, 0.0, &ctx);
    CHECK(bigfloat_ball_add(&r, &a, &b, &ctx) == BIGFLOAT_OK_E);
    CHECK(contains_d(&r, 3.0, &ctx));
    CHECK(!contains_d(&r, 3.0001, &ctx));

    /* 区间加法 [1,1.5]+[0.5,1]=[1.5,2.5] */
    mk(&a, 1.25, 0.25, &ctx);
    mk(&b, 0.75, 0.25, &ctx);
    CHECK(bigfloat_ball_add(&r, &a, &b, &ctx) == BIGFLOAT_OK_E);
    CHECK(contains_d(&r, 1.5, &ctx));
    CHECK(contains_d(&r, 2.5, &ctx));
    CHECK(!contains_d(&r, 2.6, &ctx));
    CHECK(!contains_d(&r, 1.4, &ctx));

    /* 乘法 [1,2]×[3,4]=[3,8] */
    mk(&a, 1.5, 0.5, &ctx);
    mk(&b, 3.5, 0.5, &ctx);
    CHECK(bigfloat_ball_mul(&r, &a, &b, &ctx) == BIGFLOAT_OK_E);
    CHECK(contains_d(&r, 3.0, &ctx));
    CHECK(contains_d(&r, 8.0, &ctx));
    CHECK(!contains_d(&r, 8.1, &ctx));
    CHECK(!contains_d(&r, 2.9, &ctx));

    /* 减法 [1,2]−[0,1]=[0,2] */
    mk(&a, 1.5, 0.5, &ctx);
    mk(&b, 0.5, 0.5, &ctx);
    CHECK(bigfloat_ball_sub(&r, &a, &b, &ctx) == BIGFLOAT_OK_E);
    CHECK(contains_d(&r, 0.0, &ctx));
    CHECK(contains_d(&r, 2.0, &ctx));
    CHECK(!contains_d(&r, 2.1, &ctx));

    /* 除法 [6,8]/[2,4]=[1.5,4] */
    mk(&a, 7.0, 1.0, &ctx);
    mk(&b, 3.0, 1.0, &ctx);
    CHECK(bigfloat_ball_div(&r, &a, &b, &ctx) == BIGFLOAT_OK_E);
    CHECK(contains_d(&r, 1.5, &ctx));
    CHECK(contains_d(&r, 4.0, &ctx));
    CHECK(!contains_d(&r, 4.1, &ctx));

    /* 分母含 0 -> INVALID */
    mk(&b, 0.0, 1.0, &ctx);
    CHECK(bigfloat_ball_div(&r, &a, &b, &ctx) == BIGFLOAT_ERR_INVALID_E);

    /* rad < 0 -> INVALID */
    bigfloat_ty m;
    bigfloat_ty rad;
    bigfloat_init(&m);
    bigfloat_init(&rad);
    bigfloat_from_f64(&m, 0.0);
    bigfloat_from_f64(&rad, -1.0);
    CHECK(bigfloat_ball_set(&r, &m, &rad, &ctx) == BIGFLOAT_ERR_INVALID_E);
    bigfloat_free(&rad);
    bigfloat_free(&m);

    /* 深拷贝与别名 */
    mk(&a, 3.0, 0.5, &ctx);
    CHECK(bigfloat_ball_copy(&b, &a) == BIGFLOAT_OK_E);
    CHECK(contains_d(&b, 3.0, &ctx));
    CHECK(bigfloat_ball_copy(&b, &b) == BIGFLOAT_OK_E);
    CHECK(contains_d(&b, 3.5, &ctx));

    bigfloat_ball_free(&r);
    bigfloat_ball_free(&b);
    bigfloat_ball_free(&a);

    test_decimal_ball();
    test_elem();
    test_elem_decimal();
    test_ball_series();
    test_ball_series_decimal();

    if (failures == 0) {
        printf("test_ball: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
