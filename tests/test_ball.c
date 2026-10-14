/*
 * test_ball.c：bigfloat 球式（中心 + 半径）严格区间算术（TODO §C2 路线 B）。
 *
 * 覆盖 set/contains、加/减/乘/除的区间传播、除零区间与非法参数。
 */

#include <stdio.h>

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

    if (failures == 0) {
        printf("test_ball: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
