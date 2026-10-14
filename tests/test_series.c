/*
 * test_series.c：bigfrac 截断幂级数 / Laurent 级数单元测试（TODO §C2）。
 *
 * 覆盖：构造/去尾零、加、减、乘（截断）、倒数（含 Laurent）、求值（含负幂）。
 */

#include <stdio.h>
#include <string.h>

#include "nex/bigfrac/nex_bigfrac.h"

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static void build(bigfrac_series_ty *s, int64_t val,
        const char *const *strs, size_t n) {
    bigfrac_ty c[8];
    for (size_t i = 0U; i < n; i++) {
        bigfrac_init(&c[i]);
        bigfrac_from_str(&c[i], strs[i], NULL);
    }
    bigfrac_series_from_array(s, val, c, n);
    for (size_t i = 0U; i < n; i++) {
        bigfrac_free(&c[i]);
    }
}

static int series_eq(const bigfrac_series_ty *s, int64_t val,
        const char *const *strs, size_t n) {
    if (n == 0U) {
        return s->len == 0U;
    }
    if (s->val != val || s->len != n) {
        return 0;
    }
    for (size_t i = 0U; i < n; i++) {
        bigfrac_ty c;
        bigfrac_init(&c);
        bigfrac_from_str(&c, strs[i], NULL);
        int eq = (bigfrac_cmp(&s->coeffs[i], &c) == 0);
        bigfrac_free(&c);
        if (!eq) {
            return 0;
        }
    }
    return 1;
}

static void test_add_mul(void) {
    const char *f[] = {"1", "1", "1"};   /* 1 + z + z^2 */
    bigfrac_series_ty a, b, r;
    bigfrac_series_init(&a);
    bigfrac_series_init(&b);
    bigfrac_series_init(&r);
    build(&a, 0, f, 3U);

    /* f + f = 2 + 2z + 2z^2 */
    const char *ff[] = {"2", "2", "2"};
    CHECK(bigfrac_series_add(&r, &a, &a, 8U) == BIGFRAC_OK_E);
    CHECK(series_eq(&r, 0, ff, 3U));

    /* f - f = 0 */
    CHECK(bigfrac_series_sub(&r, &a, &a, 8U) == BIGFRAC_OK_E);
    CHECK(r.len == 0U);

    /* f*f = 1 + 2z + 3z^2 + 2z^3 + z^4 */
    const char *fsq[] = {"1", "2", "3", "2", "1"};
    CHECK(bigfrac_series_mul(&r, &a, &a, 8U) == BIGFRAC_OK_E);
    CHECK(series_eq(&r, 0, fsq, 5U));

    /* 截断到 3 项：1 + 2z + 3z^2 */
    const char *fsq3[] = {"1", "2", "3"};
    CHECK(bigfrac_series_mul(&r, &a, &a, 3U) == BIGFRAC_OK_E);
    CHECK(series_eq(&r, 0, fsq3, 3U));

    /* 去尾零：f = 1 + z + 0·z^2 -> 归一为 1 + z */
    const char *g[] = {"1", "1", "0"};
    build(&b, 0, g, 3U);
    CHECK(b.len == 2U && b.val == 0);

    bigfrac_series_free(&r);
    bigfrac_series_free(&b);
    bigfrac_series_free(&a);
}

static void test_inv_laurent(void) {
    const char *f[] = {"1", "1", "1"};
    bigfrac_series_ty a, r;
    bigfrac_series_init(&a);
    bigfrac_series_init(&r);
    build(&a, 0, f, 3U);

    /* 1/(1+z+z^2) = 1 - z + 0z^2 + z^3 - z^4 + ... */
    const char *inv4[] = {"1", "-1", "0", "1"};
    CHECK(bigfrac_series_inv(&r, &a, 4U) == BIGFRAC_OK_E);
    CHECK(series_eq(&r, 0, inv4, 4U));

    /* Laurent：z^{-1} 的倒数 = z */
    const char *h[] = {"1"};
    build(&a, -1, h, 1U);                     /* z^{-1} */
    CHECK(bigfrac_series_inv(&r, &a, 4U) == BIGFRAC_OK_E);
    const char *z1[] = {"1"};
    CHECK(series_eq(&r, 1, z1, 1U));          /* z，val = 1 */

    /* z^{-1} × z = 1 */
    bigfrac_series_ty z;
    bigfrac_series_init(&z);
    build(&z, 1, h, 1U);                       /* z */
    CHECK(bigfrac_series_mul(&r, &a, &z, 4U) == BIGFRAC_OK_E);
    CHECK(series_eq(&r, 0, z1, 1U));          /* 1 */

    bigfrac_series_free(&z);
    bigfrac_series_free(&r);
    bigfrac_series_free(&a);
}

static void test_eval(void) {
    const char *f[] = {"1", "1", "1"};
    bigfrac_series_ty a;
    bigfrac_series_init(&a);
    build(&a, 0, f, 3U);

    bigfrac_ty x, out, want;
    bigfrac_init(&x);
    bigfrac_init(&out);
    bigfrac_init(&want);

    bigfrac_from_str(&x, "2", NULL);           /* 1 + 2 + 4 = 7 */
    CHECK(bigfrac_series_eval(&out, &a, &x) == BIGFRAC_OK_E);
    bigfrac_from_str(&want, "7", NULL);
    CHECK(bigfrac_cmp(&out, &want) == 0);

    /* Laurent z^{-1} at x = 2 -> 1/2 */
    const char *h[] = {"1"};
    build(&a, -1, h, 1U);
    bigfrac_from_str(&x, "2", NULL);
    CHECK(bigfrac_series_eval(&out, &a, &x) == BIGFRAC_OK_E);
    bigfrac_from_str(&want, "1/2", NULL);
    CHECK(bigfrac_cmp(&out, &want) == 0);

    /* x = 0 且 val < 0 -> INVALID */
    bigfrac_from_str(&x, "0", NULL);
    CHECK(bigfrac_series_eval(&out, &a, &x) == BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&want);
    bigfrac_free(&out);
    bigfrac_free(&x);
    bigfrac_series_free(&a);
}

int main(void) {
    test_add_mul();
    test_inv_laurent();
    test_eval();

    if (failures == 0) {
        printf("test_series: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
