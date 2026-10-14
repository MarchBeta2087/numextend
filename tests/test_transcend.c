/*
 * test_transcend.c：超越函数单元测试（TODO §D）——正确舍入的 atan。
 */

#include <stdio.h>

#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/transcend/nex_transcend.h"

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

static void check_atan(const char *in, const char *want,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ty x;
    bigfloat_ty out;
    bigfloat_ty w;
    bigfloat_init(&x);
    bigfloat_init(&out);
    bigfloat_init(&w);
    (void)bigfloat_from_str(&x, in, ctx, NULL);
    (void)bigfloat_from_str(&w, want, ctx, NULL);
    bigfloat_err_ty err = bigfloat_atan(&out, &x, ctx);
    if (err != BIGFLOAT_OK_E) {
        printf("FAIL atan(%s) err=%d\n", in, (int)err);
        ++failures;
    } else if (!bigfloat_eq(&out, &w)) {
        printf("FAIL atan(%s) != %s\n", in, want);
        ++failures;
    }
    bigfloat_free(&w);
    bigfloat_free(&out);
    bigfloat_free(&x);
}

int main(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();

    /* 正确舍入到 binary64（参考为数学库/已知正确舍入值） */
    check_atan("0", "0", &ctx);
    check_atan("1", "0.7853981633974483", &ctx);      /* pi/4 */
    check_atan("-1", "-0.7853981633974483", &ctx);
    check_atan("0.5", "0.4636476090008061", &ctx);
    check_atan("2", "1.1071487177940904", &ctx);
    check_atan("3", "1.2490457723982544", &ctx);
    check_atan("0.25", "0.24497866312686414", &ctx);
    check_atan("10", "1.4711276743037347", &ctx);

    /* 更大精度（binary128）：atan(1)=pi/4 自洽——atan(1)·2 = pi/2 与
       2·atan(1) 对照（此处仅验证有限、无报错与单调性） */
    bigfloat_ctx_ty ctx128 = bigfloat_ctx_binary128();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_init(&a);
    bigfloat_init(&b);
    (void)bigfloat_from_str(&a, "1", &ctx128, NULL);
    (void)bigfloat_from_str(&b, "2", &ctx128, NULL);
    bigfloat_ty oa;
    bigfloat_ty ob;
    bigfloat_init(&oa);
    bigfloat_init(&ob);
    CHECK(bigfloat_atan(&oa, &a, &ctx128) == BIGFLOAT_OK_E);
    CHECK(bigfloat_atan(&ob, &b, &ctx128) == BIGFLOAT_OK_E);
    CHECK(bigfloat_cmp(&oa, &ob) < 0);   /* atan 单调增：1 < 2 */
    bigfloat_free(&ob);
    bigfloat_free(&oa);
    bigfloat_free(&b);
    bigfloat_free(&a);

    /* 非法参数 */
    bigfloat_ty x;
    bigfloat_init(&x);
    CHECK(bigfloat_atan(NULL, &x, &ctx) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_atan(&x, NULL, &ctx) == BIGFLOAT_ERR_INVALID_E);
    CHECK(bigfloat_atan(&x, &x, NULL) == BIGFLOAT_ERR_INVALID_E);
    bigfloat_free(&x);

    if (failures == 0) {
        printf("test_transcend: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
