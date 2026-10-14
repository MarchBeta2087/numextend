/*
 * test_binom.c：广义二项式系数与二项式级数单元测试（TODO §C2）。
 *
 *   C(alpha,k) = alpha(alpha-1)...(alpha-k+1)/k!
 *   (1+z)^alpha = Σ C(alpha,k) z^k
 */

#include <stdio.h>

#include "nex/bigfrac/nex_bigfrac.h"

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

static void test_coeff(void) {
    bigfrac_ty a;
    bigfrac_ty c;
    bigfrac_init(&a);
    bigfrac_init(&c);

    bigfrac_from_str(&a, "5", NULL);           /* 整数 n */
    CHECK(bigfrac_binom(&c, &a, 2U) == BIGFRAC_OK_E && frac_eq_str(&c, "10"));
    CHECK(bigfrac_binom(&c, &a, 0U) == BIGFRAC_OK_E && frac_eq_str(&c, "1"));
    CHECK(bigfrac_binom(&c, &a, 6U) == BIGFRAC_OK_E && frac_eq_str(&c, "0"));

    bigfrac_from_str(&a, "1/2", NULL);         /* C(1/2,2) = -1/8 */
    CHECK(bigfrac_binom(&c, &a, 2U) == BIGFRAC_OK_E && frac_eq_str(&c, "-1/8"));

    bigfrac_from_str(&a, "-1", NULL);          /* C(-1,k) = (-1)^k */
    CHECK(bigfrac_binom(&c, &a, 3U) == BIGFRAC_OK_E && frac_eq_str(&c, "-1"));

    bigfrac_from_str(&a, "1/3", NULL);         /* C(1/3,3) = 5/81 */
    CHECK(bigfrac_binom(&c, &a, 3U) == BIGFRAC_OK_E && frac_eq_str(&c, "5/81"));

    CHECK(bigfrac_binom(NULL, &a, 1U) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_binom(&c, NULL, 1U) == BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&c);
    bigfrac_free(&a);
}

static void test_series(void) {
    bigfrac_ty a;
    bigfrac_series_ty s;
    bigfrac_series_ty sq;
    bigfrac_init(&a);
    bigfrac_series_init(&s);
    bigfrac_series_init(&sq);

    /* (1+z)^{-1} = 1 - z + z^2 - z^3 */
    bigfrac_from_str(&a, "-1", NULL);
    CHECK(bigfrac_series_binom(&s, &a, 4U) == BIGFRAC_OK_E);
    CHECK(s.val == 0 && s.len == 4U);
    CHECK(frac_eq_str(&s.coeffs[0], "1") && frac_eq_str(&s.coeffs[1], "-1") &&
          frac_eq_str(&s.coeffs[2], "1") && frac_eq_str(&s.coeffs[3], "-1"));

    /* (1+z)^{1/2} 平方（截断 3 项）= 1 + z */
    bigfrac_from_str(&a, "1/2", NULL);
    CHECK(bigfrac_series_binom(&s, &a, 3U) == BIGFRAC_OK_E);
    CHECK(bigfrac_series_mul(&sq, &s, &s, 3U) == BIGFRAC_OK_E);
    CHECK(sq.val == 0 && sq.len == 2U);
    CHECK(frac_eq_str(&sq.coeffs[0], "1") && frac_eq_str(&sq.coeffs[1], "1"));

    /* (1+z)^3 = 1 + 3z + 3z^2 + z^3 */
    bigfrac_from_str(&a, "3", NULL);
    CHECK(bigfrac_series_binom(&s, &a, 4U) == BIGFRAC_OK_E);
    CHECK(s.val == 0 && s.len == 4U);
    CHECK(frac_eq_str(&s.coeffs[0], "1") && frac_eq_str(&s.coeffs[1], "3") &&
          frac_eq_str(&s.coeffs[2], "3") && frac_eq_str(&s.coeffs[3], "1"));

    /* 阶数大于次数：高次零被去尾（n=3、order=6 仍 len=4） */
    CHECK(bigfrac_series_binom(&s, &a, 6U) == BIGFRAC_OK_E);
    CHECK(s.len == 4U);

    /* order = 0 -> 零级数 */
    CHECK(bigfrac_series_binom(&s, &a, 0U) == BIGFRAC_OK_E);
    CHECK(s.len == 0U);

    CHECK(bigfrac_series_binom(NULL, &a, 1U) == BIGFRAC_ERR_INVALID_E);

    bigfrac_series_free(&sq);
    bigfrac_series_free(&s);
    bigfrac_free(&a);
}

int main(void) {
    test_coeff();
    test_series();

    if (failures == 0) {
        printf("test_binom: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
