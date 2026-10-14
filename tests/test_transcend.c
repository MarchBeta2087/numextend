/*
 * test_transcend.c：超越函数单元测试（TODO §D）——正确舍入的 atan。
 */

#include <stdio.h>

#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
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

static void check_atan_dec(const char *in, const char *want,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty x;
    bigdecimal_ty out;
    bigdecimal_ty w;
    bigdecimal_init(&x);
    bigdecimal_init(&out);
    bigdecimal_init(&w);
    (void)bigdecimal_from_str(&x, in, ctx, NULL);
    (void)bigdecimal_from_str(&w, want, ctx, NULL);
    bigdecimal_err_ty err = bigdecimal_atan(&out, &x, ctx);
    if (err != BIGDECIMAL_OK_E) {
        printf("FAIL dec atan(%s) err=%d\n", in, (int)err);
        ++failures;
    } else if (!bigdecimal_eq(&out, &w)) {
        printf("FAIL dec atan(%s) != %s\n", in, want);
        ++failures;
    }
    bigdecimal_free(&w);
    bigdecimal_free(&out);
    bigdecimal_free(&x);
}

static void check_exp(const char *in, const char *want,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ty x;
    bigfloat_ty out;
    bigfloat_ty w;
    bigfloat_init(&x);
    bigfloat_init(&out);
    bigfloat_init(&w);
    (void)bigfloat_from_str(&x, in, ctx, NULL);
    (void)bigfloat_from_str(&w, want, ctx, NULL);
    bigfloat_err_ty err = bigfloat_exp(&out, &x, ctx);
    if (err != BIGFLOAT_OK_E) {
        printf("FAIL exp(%s) err=%d\n", in, (int)err);
        ++failures;
    } else if (!bigfloat_eq(&out, &w)) {
        printf("FAIL exp(%s) != %s\n", in, want);
        ++failures;
    }
    bigfloat_free(&w);
    bigfloat_free(&out);
    bigfloat_free(&x);
}

static void check_exp_dec(const char *in, const char *want,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty x;
    bigdecimal_ty out;
    bigdecimal_ty w;
    bigdecimal_init(&x);
    bigdecimal_init(&out);
    bigdecimal_init(&w);
    (void)bigdecimal_from_str(&x, in, ctx, NULL);
    (void)bigdecimal_from_str(&w, want, ctx, NULL);
    bigdecimal_err_ty err = bigdecimal_exp(&out, &x, ctx);
    if (err != BIGDECIMAL_OK_E) {
        printf("FAIL dec exp(%s) err=%d\n", in, (int)err);
        ++failures;
    } else if (!bigdecimal_eq(&out, &w)) {
        printf("FAIL dec exp(%s) != %s\n", in, want);
        ++failures;
    }
    bigdecimal_free(&w);
    bigdecimal_free(&out);
    bigdecimal_free(&x);
}

static void check_log(const char *in, const char *want,
        const bigfloat_ctx_ty *ctx) {
    bigfloat_ty x;
    bigfloat_ty out;
    bigfloat_ty w;
    bigfloat_init(&x);
    bigfloat_init(&out);
    bigfloat_init(&w);
    (void)bigfloat_from_str(&x, in, ctx, NULL);
    (void)bigfloat_from_str(&w, want, ctx, NULL);
    bigfloat_err_ty err = bigfloat_log(&out, &x, ctx);
    if (err != BIGFLOAT_OK_E) {
        printf("FAIL log(%s) err=%d\n", in, (int)err);
        ++failures;
    } else if (!bigfloat_eq(&out, &w)) {
        printf("FAIL log(%s) != %s\n", in, want);
        ++failures;
    }
    bigfloat_free(&w);
    bigfloat_free(&out);
    bigfloat_free(&x);
}

static void check_log_dec(const char *in, const char *want,
        const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty x;
    bigdecimal_ty out;
    bigdecimal_ty w;
    bigdecimal_init(&x);
    bigdecimal_init(&out);
    bigdecimal_init(&w);
    (void)bigdecimal_from_str(&x, in, ctx, NULL);
    (void)bigdecimal_from_str(&w, want, ctx, NULL);
    bigdecimal_err_ty err = bigdecimal_log(&out, &x, ctx);
    if (err != BIGDECIMAL_OK_E) {
        printf("FAIL dec log(%s) err=%d\n", in, (int)err);
        ++failures;
    } else if (!bigdecimal_eq(&out, &w)) {
        printf("FAIL dec log(%s) != %s\n", in, want);
        ++failures;
    }
    bigdecimal_free(&w);
    bigdecimal_free(&out);
    bigdecimal_free(&x);
}

typedef bigfloat_err_ty (*bf_fn1)(bigfloat_ty *, const bigfloat_ty *,
        const bigfloat_ctx_ty *);

typedef bigdecimal_err_ty (*bd_fn1)(bigdecimal_ty *, const bigdecimal_ty *,
        const bigdecimal_ctx_ty *);

static void check_trig(const char *desc, const char *in, const char *want,
        bf_fn1 fn, const bigfloat_ctx_ty *ctx) {
    bigfloat_ty x;
    bigfloat_ty out;
    bigfloat_ty w;
    bigfloat_init(&x);
    bigfloat_init(&out);
    bigfloat_init(&w);
    (void)bigfloat_from_str(&x, in, ctx, NULL);
    (void)bigfloat_from_str(&w, want, ctx, NULL);
    bigfloat_err_ty err = fn(&out, &x, ctx);
    if (err != BIGFLOAT_OK_E) {
        printf("FAIL %s(%s) err=%d\n", desc, in, (int)err);
        ++failures;
    } else if (!bigfloat_eq(&out, &w)) {
        printf("FAIL %s(%s) != %s\n", desc, in, want);
        ++failures;
    }
    bigfloat_free(&w);
    bigfloat_free(&out);
    bigfloat_free(&x);
}

static void check_trig_dec(const char *desc, const char *in,
        const char *want, bd_fn1 fn, const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty x;
    bigdecimal_ty out;
    bigdecimal_ty w;
    bigdecimal_init(&x);
    bigdecimal_init(&out);
    bigdecimal_init(&w);
    (void)bigdecimal_from_str(&x, in, ctx, NULL);
    (void)bigdecimal_from_str(&w, want, ctx, NULL);
    bigdecimal_err_ty err = fn(&out, &x, ctx);
    if (err != BIGDECIMAL_OK_E) {
        printf("FAIL dec %s(%s) err=%d\n", desc, in, (int)err);
        ++failures;
    } else if (!bigdecimal_eq(&out, &w)) {
        printf("FAIL dec %s(%s) != %s\n", desc, in, want);
        ++failures;
    }
    bigdecimal_free(&w);
    bigdecimal_free(&out);
    bigdecimal_free(&x);
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

    /* 十进制正确舍入到 decimal64（16 位有效数字） */
    const bigdecimal_ctx_ty dctx = bigdecimal_ctx_decimal64();
    check_atan_dec("0", "0", &dctx);
    check_atan_dec("1", "0.7853981633974483", &dctx);      /* pi/4 */
    check_atan_dec("-1", "-0.7853981633974483", &dctx);
    check_atan_dec("0.5", "0.4636476090008061", &dctx);
    check_atan_dec("2", "1.107148717794091", &dctx);
    check_atan_dec("3", "1.249045772398254", &dctx);
    check_atan_dec("0.25", "0.2449786631268642", &dctx);
    check_atan_dec("10", "1.471127674303735", &dctx);

    /* exp：正确舍入（binary64 / decimal64） */
    check_exp("0", "1", &ctx);
    check_exp("1", "2.718281828459045", &ctx);
    check_exp("-1", "0.36787944117144233", &ctx);
    check_exp("0.5", "1.6487212707001282", &ctx);
    check_exp("2", "7.38905609893065", &ctx);
    check_exp("10", "22026.465794806718", &ctx);
    check_exp_dec("0", "1", &dctx);
    check_exp_dec("1", "2.718281828459045", &dctx);
    check_exp_dec("-1", "0.3678794411714423", &dctx);
    check_exp_dec("0.5", "1.648721270700128", &dctx);
    check_exp_dec("10", "22026.46579480672", &dctx);

    /* log：正确舍入（binary64 / decimal64） */
    check_log("1", "0", &ctx);
    check_log("2", "0.6931471805599453", &ctx);
    check_log("10", "2.302585092994046", &ctx);
    check_log("0.5", "-0.6931471805599453", &ctx);
    check_log("0.1", "-2.3025850929940455", &ctx);
    check_log("7", "1.9459101490553132", &ctx);
    check_log_dec("1", "0", &dctx);
    check_log_dec("2", "0.6931471805599453", &dctx);
    check_log_dec("10", "2.302585092994046", &dctx);
    check_log_dec("0.5", "-0.6931471805599453", &dctx);
    check_log_dec("1.5", "0.4054651081081644", &dctx);
    check_log_dec("7", "1.945910149055313", &dctx);

    /* sin / cos / tan：正确舍入（binary64 / decimal64） */
    check_trig("sin", "0", "0", bigfloat_sin, &ctx);
    check_trig("sin", "1", "0.8414709848078965", bigfloat_sin, &ctx);
    check_trig("sin", "0.5", "0.479425538604203", bigfloat_sin, &ctx);
    check_trig("sin", "-2", "-0.9092974268256817", bigfloat_sin, &ctx);
    check_trig("cos", "0", "1", bigfloat_cos, &ctx);
    check_trig("cos", "1", "0.5403023058681398", bigfloat_cos, &ctx);
    check_trig("cos", "0.5", "0.8775825618903728", bigfloat_cos, &ctx);
    check_trig("cos", "-2", "-0.4161468365471424", bigfloat_cos, &ctx);
    check_trig("tan", "0", "0", bigfloat_tan, &ctx);
    check_trig("tan", "1", "1.5574077246549023", bigfloat_tan, &ctx);
    check_trig("tan", "0.5", "0.5463024898437905", bigfloat_tan, &ctx);
    check_trig("tan", "-2", "2.185039863261519", bigfloat_tan, &ctx);
    check_trig_dec("sin", "1", "0.8414709848078965", bigdecimal_sin, &dctx);
    check_trig_dec("sin", "2", "0.9092974268256817", bigdecimal_sin, &dctx);
    check_trig_dec("cos", "1", "0.5403023058681397", bigdecimal_cos, &dctx);
    check_trig_dec("cos", "2", "-0.4161468365471424", bigdecimal_cos, &dctx);
    check_trig_dec("tan", "1", "1.557407724654902", bigdecimal_tan, &dctx);
    check_trig_dec("tan", "0.5", "0.5463024898437905", bigdecimal_tan, &dctx);

    /* asin / acos：正确舍入（binary64 / decimal64） */
    check_trig("asin", "0", "0", bigfloat_asin, &ctx);
    check_trig("asin", "0.5", "0.5235987755982989", bigfloat_asin, &ctx);
    check_trig("asin", "-0.5", "-0.5235987755982989", bigfloat_asin, &ctx);
    check_trig("asin", "1", "1.5707963267948966", bigfloat_asin, &ctx);
    check_trig("asin", "-1", "-1.5707963267948966", bigfloat_asin, &ctx);
    check_trig("acos", "0", "1.5707963267948966", bigfloat_acos, &ctx);
    check_trig("acos", "0.5", "1.0471975511965979", bigfloat_acos, &ctx);
    check_trig("acos", "-0.5", "2.0943951023931957", bigfloat_acos, &ctx);
    check_trig("acos", "1", "0", bigfloat_acos, &ctx);
    check_trig("acos", "-1", "3.141592653589793", bigfloat_acos, &ctx);
    check_trig_dec("asin", "0.5", "0.5235987755982989", bigdecimal_asin,
            &dctx);
    check_trig_dec("asin", "-1", "-1.570796326794897", bigdecimal_asin,
            &dctx);
    check_trig_dec("acos", "0", "1.570796326794897", bigdecimal_acos, &dctx);
    check_trig_dec("acos", "0.5", "1.047197551196598", bigdecimal_acos,
            &dctx);
    check_trig_dec("acos", "-1", "3.141592653589793", bigdecimal_acos,
            &dctx);
    /* log 定义域 */
    {
        bigfloat_ty nx;
        bigfloat_init(&nx);
        (void)bigfloat_from_str(&nx, "-1", &ctx, NULL);
        CHECK(bigfloat_log(&nx, &nx, &ctx) == BIGFLOAT_ERR_INVALID_E);
        bigfloat_free(&nx);
    }

    /* 非法参数（十进制） */
    {
        bigdecimal_ty dx;
        bigdecimal_init(&dx);
        CHECK(bigdecimal_atan(NULL, &dx, &dctx) == BIGDECIMAL_ERR_INVALID_E);
        CHECK(bigdecimal_atan(&dx, NULL, &dctx) == BIGDECIMAL_ERR_INVALID_E);
        CHECK(bigdecimal_atan(&dx, &dx, NULL) == BIGDECIMAL_ERR_INVALID_E);
        bigdecimal_free(&dx);
    }

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
