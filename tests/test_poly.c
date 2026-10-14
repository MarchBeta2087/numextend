/*
 * test_poly.c：多项式求值（秦九韶 / Horner）单元测试（TODO §C1）。
 *
 * 覆盖 bigint_bin / bigfrac / bigfloat / bigdecimal：
 *   - 基本求值、degree 0、x = 0、负 x；
 *   - 别名（out 与 x 同一对象）；
 *   - NULL 参数错误码；
 *   - bigdecimal 的十进制精确性（1 + 0.1 + 0.01 = 1.11）。
 */

#include <stdio.h>
#include <string.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static void test_bigint(void) {
    bigint_bin_ty a[3];
    bigint_bin_ty x, out, want;
    for (int i = 0; i < 3; i++) {
        bigint_bin_init(&a[i]);
    }
    bigint_bin_init(&x);
    bigint_bin_init(&out);
    bigint_bin_init(&want);

    /* p(x) = 1 + 2x + 3x^2 */
    bigint_bin_from_u64(&a[0], 1U);
    bigint_bin_from_u64(&a[1], 2U);
    bigint_bin_from_u64(&a[2], 3U);

    bigint_bin_from_u64(&x, 5U);              /* 1 + 10 + 75 = 86 */
    CHECK(bigint_bin_poly_eval(&out, a, 2U, &x) == BIGINT_OK_E);
    bigint_bin_from_u64(&want, 86U);
    CHECK(bigint_bin_cmp(&out, &want) == 0);

    bigint_bin_from_i64(&x, -2);              /* 1 - 4 + 12 = 9 */
    CHECK(bigint_bin_poly_eval(&out, a, 2U, &x) == BIGINT_OK_E);
    bigint_bin_from_u64(&want, 9U);
    CHECK(bigint_bin_cmp(&out, &want) == 0);

    bigint_bin_from_u64(&x, 0U);              /* -> a[0] = 1 */
    CHECK(bigint_bin_poly_eval(&out, a, 2U, &x) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&out, &a[0]) == 0);

    bigint_bin_from_u64(&x, 123U);            /* degree 0 -> a[0] */
    CHECK(bigint_bin_poly_eval(&out, a, 0U, &x) == BIGINT_OK_E);
    CHECK(bigint_bin_cmp(&out, &a[0]) == 0);

    bigint_bin_from_u64(&x, 5U);              /* 别名 out == x */
    CHECK(bigint_bin_poly_eval(&x, a, 2U, &x) == BIGINT_OK_E);
    bigint_bin_from_u64(&want, 86U);
    CHECK(bigint_bin_cmp(&x, &want) == 0);

    CHECK(bigint_bin_poly_eval(NULL, a, 2U, &x) == BIGINT_ERR_INVALID_E);
    CHECK(bigint_bin_poly_eval(&out, NULL, 2U, &x) == BIGINT_ERR_INVALID_E);

    bigint_bin_free(&want);
    bigint_bin_free(&out);
    bigint_bin_free(&x);
    for (int i = 0; i < 3; i++) {
        bigint_bin_free(&a[i]);
    }
}

static void test_bigfrac(void) {
    bigfrac_ty a[2];
    bigfrac_ty x, out, want;
    bigfrac_init(&a[0]);
    bigfrac_init(&a[1]);
    bigfrac_init(&x);
    bigfrac_init(&out);
    bigfrac_init(&want);

    /* p(x) = 1/2 + 1/3·x，x = 6 -> 1/2 + 2 = 5/2 */
    bigfrac_from_str(&a[0], "1/2", NULL);
    bigfrac_from_str(&a[1], "1/3", NULL);
    bigfrac_from_str(&x, "6", NULL);
    CHECK(bigfrac_poly_eval(&out, a, 1U, &x) == BIGFRAC_OK_E);
    bigfrac_from_str(&want, "5/2", NULL);
    CHECK(bigfrac_cmp(&out, &want) == 0);

    bigfrac_from_str(&x, "6", NULL);          /* 别名 out == x */
    CHECK(bigfrac_poly_eval(&x, a, 1U, &x) == BIGFRAC_OK_E);
    CHECK(bigfrac_cmp(&x, &want) == 0);

    CHECK(bigfrac_poly_eval(NULL, a, 1U, &x) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_poly_eval(&out, NULL, 1U, &x) == BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&want);
    bigfrac_free(&out);
    bigfrac_free(&x);
    bigfrac_free(&a[1]);
    bigfrac_free(&a[0]);
}

static void test_bigfloat(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a[3];
    bigfloat_ty x, out, want;
    for (int i = 0; i < 3; i++) {
        bigfloat_init(&a[i]);
    }
    bigfloat_init(&x);
    bigfloat_init(&out);
    bigfloat_init(&want);

    /* p(x) = 1 + x + x^2，x = 2 -> 7（binary64 下精确） */
    bigfloat_from_f64(&a[0], 1.0);
    bigfloat_from_f64(&a[1], 1.0);
    bigfloat_from_f64(&a[2], 1.0);
    bigfloat_from_f64(&x, 2.0);
    CHECK(bigfloat_poly_eval(&out, a, 2U, &x, &ctx) == BIGFLOAT_OK_E);
    bigfloat_from_f64(&want, 7.0);
    CHECK(bigfloat_eq(&out, &want));

    bigfloat_from_f64(&x, 0.0);               /* -> a[0] = 1 */
    CHECK(bigfloat_poly_eval(&out, a, 2U, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(bigfloat_eq(&out, &a[0]));

    bigfloat_from_f64(&x, 2.0);               /* 别名 out == x */
    CHECK(bigfloat_poly_eval(&x, a, 2U, &x, &ctx) == BIGFLOAT_OK_E);
    CHECK(bigfloat_eq(&x, &want));

    CHECK(bigfloat_poly_eval(NULL, a, 2U, &x, &ctx) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_poly_eval(&out, a, 2U, &x, NULL) == BIGFLOAT_ERR_INVALID_E);

    bigfloat_free(&want);
    bigfloat_free(&out);
    bigfloat_free(&x);
    for (int i = 0; i < 3; i++) {
        bigfloat_free(&a[i]);
    }
}

static void test_bigdecimal(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a[3];
    bigdecimal_ty x, out;
    for (int i = 0; i < 3; i++) {
        bigdecimal_init(&a[i]);
    }
    bigdecimal_init(&x);
    bigdecimal_init(&out);

    /* p(t) = 1 + t + t^2，t = 0.1 -> 1.11（十进制精确） */
    bigdecimal_from_str(&a[0], "1", &ctx, NULL);
    bigdecimal_from_str(&a[1], "1", &ctx, NULL);
    bigdecimal_from_str(&a[2], "1", &ctx, NULL);
    bigdecimal_from_str(&x, "0.1", &ctx, NULL);
    CHECK(bigdecimal_poly_eval(&out, a, 2U, &x, &ctx) == BIGDECIMAL_OK_E);

    char buf[64];
    size_t need = 0U;
    CHECK(bigdecimal_to_str(&out, BIGDECIMAL_FMT_FIXED_E,
                            buf, sizeof(buf), &need) == BIGDECIMAL_OK_E);
    CHECK(strcmp(buf, "1.11") == 0);

    CHECK(bigdecimal_poly_eval(NULL, a, 2U, &x, &ctx) ==
          BIGDECIMAL_ERR_INVALID_E);
    CHECK(bigdecimal_poly_eval(&out, a, 2U, &x, NULL) ==
          BIGDECIMAL_ERR_INVALID_E);

    bigdecimal_free(&out);
    bigdecimal_free(&x);
    for (int i = 0; i < 3; i++) {
        bigdecimal_free(&a[i]);
    }
}

int main(void) {
    test_bigint();
    test_bigfrac();
    test_bigfloat();
    test_bigdecimal();

    if (failures == 0) {
        printf("test_poly: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
