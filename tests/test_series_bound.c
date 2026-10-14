/*
 * test_series_bound.c：几何型全局尾界与自适应截断阶（路线 A，TODO §C2）。
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

static void test_tail(void) {
    bigfrac_ty A, B, r, out;
    bigfrac_init(&A);
    bigfrac_init(&B);
    bigfrac_init(&r);
    bigfrac_init(&out);

    bigfrac_from_str(&A, "1", NULL);
    bigfrac_from_str(&B, "1", NULL);
    bigfrac_from_str(&r, "1/2", NULL);

    /* n=0: A/(1-q) = 2；n=1: 1；n=2: 1/2 */
    CHECK(bigfrac_series_tail_geometric(&out, &A, &B, &r, 0U) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&out, "2"));
    CHECK(bigfrac_series_tail_geometric(&out, &A, &B, &r, 1U) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&out, "1"));
    CHECK(bigfrac_series_tail_geometric(&out, &A, &B, &r, 2U) == BIGFRAC_OK_E);
    CHECK(frac_eq_str(&out, "1/2"));

    /* B·r >= 1 -> INVALID */
    bigfrac_from_str(&r, "1", NULL);
    CHECK(bigfrac_series_tail_geometric(&out, &A, &B, &r, 1U) ==
          BIGFRAC_ERR_INVALID_E);

    /* 负参数 -> INVALID */
    bigfrac_from_str(&A, "-1", NULL);
    bigfrac_from_str(&r, "1/2", NULL);
    CHECK(bigfrac_series_tail_geometric(&out, &A, &B, &r, 1U) ==
          BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&out);
    bigfrac_free(&r);
    bigfrac_free(&B);
    bigfrac_free(&A);
}

static void test_order(void) {
    bigfrac_ty A, B, r, eps;
    bigfrac_init(&A);
    bigfrac_init(&B);
    bigfrac_init(&r);
    bigfrac_init(&eps);

    bigfrac_from_str(&A, "1", NULL);
    bigfrac_from_str(&B, "1", NULL);
    bigfrac_from_str(&r, "1/2", NULL);

    size_t n = 999U;
    /* (1/2)^n <= (1/2)·eps；eps=1/1000 -> 2^n >= 2000 -> n=11 */
    bigfrac_from_str(&eps, "1/1000", NULL);
    CHECK(bigfrac_series_order_geometric(&n, &A, &B, &r, &eps) == BIGFRAC_OK_E);
    CHECK(n == 11U);

    /* eps=1/2 -> 2^n >= 4 -> n=2 */
    bigfrac_from_str(&eps, "1/2", NULL);
    CHECK(bigfrac_series_order_geometric(&n, &A, &B, &r, &eps) == BIGFRAC_OK_E);
    CHECK(n == 2U);

    /* A=0 -> 尾界恒为 0 -> n=0 */
    bigfrac_from_str(&A, "0", NULL);
    CHECK(bigfrac_series_order_geometric(&n, &A, &B, &r, &eps) == BIGFRAC_OK_E);
    CHECK(n == 0U);

    /* B·r >= 1 -> INVALID */
    bigfrac_from_str(&A, "1", NULL);
    bigfrac_from_str(&r, "1", NULL);
    CHECK(bigfrac_series_order_geometric(&n, &A, &B, &r, &eps) ==
          BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&eps);
    bigfrac_free(&r);
    bigfrac_free(&B);
    bigfrac_free(&A);
}

int main(void) {
    test_tail();
    test_order();

    if (failures == 0) {
        printf("test_series_bound: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
