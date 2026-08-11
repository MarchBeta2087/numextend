/* OOM 注入测试：遍历每个运算的分配失败点，验证
 *   1. 无崩溃、无堆损坏（配合 MSVC 调试堆检测）；
 *   2. 返回 OOM 时输出参数保持调用前状态（强异常安全，§3.1/§11）；
 *   3. 注入下仍返回 OK 时，结果必须与无注入时完全一致
 *      （捕捉被防御性吞掉的 OOM——那会使输出违反规范化不变量）。
 *
 * 注入方式：nex_test_alloc_fail_after = N，第 N 次及之后的分配返回 NULL
 * （见 nex/nex_alloc.h）；每轮后重置为 SIZE_MAX。固定种子，确定性。
 */
#include "nex/nex_alloc.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"
#include "nex/convert/nex_convert.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

#define SWEEP_MAX 64U

/* 恢复检查：注入结束后库应能正常继续工作 */
#define RECOVER_CHECK() do { \
    nex_test_alloc_fail_after = SIZE_MAX; \
} while (0)

/* ------------------------------------------------------------------ */
/* bigfloat                                                           */
/* ------------------------------------------------------------------ */

static void sweep_bf_mul(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a, b, before, expected;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&before);
    bigfloat_init(&expected);
    bigfloat_from_str(&a, "3.141592653589793", &ctx, NULL);
    bigfloat_from_str(&b, "2.718281828459045", &ctx, NULL);
    bigfloat_mul(&expected, &a, &b, &ctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigfloat_copy(&before, &expected);
        nex_test_alloc_fail_after = fail;
        const bigfloat_err_ty e = bigfloat_mul(&before, &a, &b, &ctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGFLOAT_ERR_OOM_E) {
            CHECK(bigfloat_eq(&before, &expected));  /* dst 不变 */
        } else {
            CHECK(e == BIGFLOAT_OK_E);
            CHECK(bigfloat_eq(&before, &expected));
        }
    }
    /* 恢复验证 */
    nex_test_alloc_fail_after = SIZE_MAX;
    CHECK(bigfloat_mul(&before, &a, &b, &ctx) == BIGFLOAT_OK_E);
    CHECK(bigfloat_eq(&before, &expected));
    printf("bigfloat mul OOM sweep: OK\n");
    bigfloat_free(&expected);
    bigfloat_free(&before);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

static void sweep_bf_div(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a, b, expected;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&expected);
    bigfloat_from_str(&a, "1", &ctx, NULL);
    bigfloat_from_str(&b, "3", &ctx, NULL);
    bigfloat_div(&expected, &a, &b, &ctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigfloat_ty dst;
        bigfloat_init(&dst);
        bigfloat_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigfloat_err_ty e = bigfloat_div(&dst, &a, &b, &ctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGFLOAT_ERR_OOM_E) {
            CHECK(bigfloat_eq(&dst, &expected));
        } else {
            CHECK(e == BIGFLOAT_OK_E);
            CHECK(bigfloat_eq(&dst, &expected));
        }
        bigfloat_free(&dst);
    }
    printf("bigfloat div OOM sweep: OK\n");
    bigfloat_free(&expected);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

static void sweep_bf_sqrt(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a, expected;
    bigfloat_init(&a);
    bigfloat_init(&expected);
    bigfloat_from_str(&a, "2", &ctx, NULL);
    bigfloat_sqrt(&expected, &a, &ctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigfloat_ty dst;
        bigfloat_init(&dst);
        bigfloat_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigfloat_err_ty e = bigfloat_sqrt(&dst, &a, &ctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGFLOAT_ERR_OOM_E) {
            CHECK(bigfloat_eq(&dst, &expected));
        } else {
            CHECK(e == BIGFLOAT_OK_E);
            CHECK(bigfloat_eq(&dst, &expected));
        }
        bigfloat_free(&dst);
    }
    printf("bigfloat sqrt OOM sweep: OK\n");
    bigfloat_free(&expected);
    bigfloat_free(&a);
}

static void sweep_bf_from_str(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty expected;
    bigfloat_init(&expected);
    bigfloat_from_str(&expected, "0.1", &ctx, NULL);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigfloat_ty dst;
        bigfloat_init(&dst);
        bigfloat_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigfloat_err_ty e = bigfloat_from_str(&dst, "0.1", &ctx, NULL);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGFLOAT_ERR_OOM_E) {
            CHECK(bigfloat_eq(&dst, &expected));
        } else {
            CHECK(e == BIGFLOAT_OK_E);
            CHECK(bigfloat_eq(&dst, &expected));
        }
        bigfloat_free(&dst);
    }
    printf("bigfloat from_str OOM sweep: OK\n");
    bigfloat_free(&expected);
}

static void sweep_bf_to_str(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_init(&a);
    bigfloat_from_str(&a, "123456789.123456789", &ctx, NULL);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        char buf[128];
        nex_test_alloc_fail_after = fail;
        const bigfloat_err_ty e = bigfloat_to_str(&a, 0, buf, sizeof(buf),
                NULL);
        nex_test_alloc_fail_after = SIZE_MAX;
        CHECK(e == BIGFLOAT_OK_E || e == BIGFLOAT_ERR_OOM_E);
    }
    /* 恢复 */
    char buf[128];
    CHECK(bigfloat_to_str(&a, 0, buf, sizeof(buf), NULL) == BIGFLOAT_OK_E);
    printf("bigfloat to_str OOM sweep: OK\n");
    bigfloat_free(&a);
}

/* ------------------------------------------------------------------ */
/* bigdecimal                                                         */
/* ------------------------------------------------------------------ */

static void sweep_bd_mul(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a, b, expected;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&expected);
    bigdecimal_from_str(&a, "3.141592653589793", &ctx, NULL);
    bigdecimal_from_str(&b, "2.718281828459045", &ctx, NULL);
    bigdecimal_mul(&expected, &a, &b, &ctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigdecimal_ty dst;
        bigdecimal_init(&dst);
        bigdecimal_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigdecimal_err_ty e = bigdecimal_mul(&dst, &a, &b, &ctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGDECIMAL_ERR_OOM_E) {
            CHECK(bigdecimal_eq(&dst, &expected));
        } else {
            CHECK(e == BIGDECIMAL_OK_E);
            CHECK(bigdecimal_eq(&dst, &expected));
        }
        bigdecimal_free(&dst);
    }
    printf("bigdecimal mul OOM sweep: OK\n");
    bigdecimal_free(&expected);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

static void sweep_bd_sqrt(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    bigdecimal_ty a, expected;
    bigdecimal_init(&a);
    bigdecimal_init(&expected);
    bigdecimal_from_str(&a, "2", &ctx, NULL);
    bigdecimal_sqrt(&expected, &a, &ctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigdecimal_ty dst;
        bigdecimal_init(&dst);
        bigdecimal_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigdecimal_err_ty e = bigdecimal_sqrt(&dst, &a, &ctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGDECIMAL_ERR_OOM_E) {
            CHECK(bigdecimal_eq(&dst, &expected));
        } else {
            CHECK(e == BIGDECIMAL_OK_E);
            CHECK(bigdecimal_eq(&dst, &expected));
        }
        bigdecimal_free(&dst);
    }
    printf("bigdecimal sqrt OOM sweep: OK\n");
    bigdecimal_free(&expected);
    bigdecimal_free(&a);
}

/* ------------------------------------------------------------------ */
/* bigfrac                                                            */
/* ------------------------------------------------------------------ */

static void sweep_frac_add(void) {
    bigfrac_ty a, b, expected;
    bigint_bin_ty n;
    bigfrac_init(&a);
    bigfrac_init(&b);
    bigfrac_init(&expected);
    bigint_bin_init(&n);
    bigint_bin_from_str(&n, "123456789123456789123456789", 10, NULL);
    bigfrac_from_ints(&a, &n, &n);
    bigint_bin_from_str(&n, "987654321987654321987654321", 10, NULL);
    bigfrac_from_ints(&b, &n, &n);
    bigfrac_add(&expected, &a, &b);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigfrac_ty dst;
        bigfrac_init(&dst);
        bigfrac_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigfrac_err_ty e = bigfrac_add(&dst, &a, &b);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGFRAC_ERR_OOM_E) {
            CHECK(bigfrac_cmp(&dst, &expected) == 0);
        } else {
            CHECK(e == BIGFRAC_OK_E);
            CHECK(bigfrac_cmp(&dst, &expected) == 0);
        }
        bigfrac_free(&dst);
    }
    printf("bigfrac add OOM sweep: OK\n");
    bigint_bin_free(&n);
    bigfrac_free(&expected);
    bigfrac_free(&b);
    bigfrac_free(&a);
}

/* ------------------------------------------------------------------ */
/* bigint_bin / bigint_dec                                            */
/* ------------------------------------------------------------------ */

static void sweep_bin_mul(void) {
    bigint_bin_ty a, b, expected;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&expected);
    bigint_bin_from_str(&a, "123456789123456789123456789123456789", 10, NULL);
    bigint_bin_from_str(&b, "987654321987654321987654321", 10, NULL);
    bigint_bin_mul(&expected, &a, &b);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigint_bin_ty dst;
        bigint_bin_init(&dst);
        bigint_bin_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigint_err_ty e = bigint_bin_mul(&dst, &a, &b);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGINT_ERR_OOM_E) {
            CHECK(bigint_bin_cmp(&dst, &expected) == 0);
        } else {
            CHECK(e == BIGINT_OK_E);
            CHECK(bigint_bin_cmp(&dst, &expected) == 0);
        }
        bigint_bin_free(&dst);
    }
    printf("bigint_bin mul OOM sweep: OK\n");
    bigint_bin_free(&expected);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

static void sweep_dec_mul(void) {
    bigint_dec_ty a, b, expected;
    bigint_dec_init(&a);
    bigint_dec_init(&b);
    bigint_dec_init(&expected);
    bigint_dec_from_str(&a, "123456789123456789123456789123456789", 10, NULL);
    bigint_dec_from_str(&b, "987654321987654321987654321", 10, NULL);
    bigint_dec_mul(&expected, &a, &b);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigint_dec_ty dst;
        bigint_dec_init(&dst);
        bigint_dec_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigint_err_ty e = bigint_dec_mul(&dst, &a, &b);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGINT_ERR_OOM_E) {
            CHECK(bigint_dec_cmp(&dst, &expected) == 0);
        } else {
            CHECK(e == BIGINT_OK_E);
            CHECK(bigint_dec_cmp(&dst, &expected) == 0);
        }
        bigint_dec_free(&dst);
    }
    printf("bigint_dec mul OOM sweep: OK\n");
    bigint_dec_free(&expected);
    bigint_dec_free(&b);
    bigint_dec_free(&a);
}

/* ------------------------------------------------------------------ */
/* convert / bigcomplex                                               */
/* ------------------------------------------------------------------ */

static void sweep_conv_float_to_dec(void) {
    const bigfloat_ctx_ty fctx = bigfloat_ctx_binary64();
    const bigdecimal_ctx_ty dctx = bigdecimal_ctx_decimal64();
    bigfloat_ty a;
    bigdecimal_ty expected;
    bigfloat_init(&a);
    bigdecimal_init(&expected);
    bigfloat_from_str(&a, "0.1", &fctx, NULL);
    nex_convert_float_to_decimal(&expected, &a, &dctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigdecimal_ty dst;
        bigdecimal_init(&dst);
        bigdecimal_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const nex_convert_err_ty e = nex_convert_float_to_decimal(&dst, &a,
                &dctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == NEX_CONVERT_ERR_OOM_E) {
            CHECK(bigdecimal_eq(&dst, &expected));
        } else {
            CHECK(e == NEX_CONVERT_OK_E);
            CHECK(bigdecimal_eq(&dst, &expected));
        }
        bigdecimal_free(&dst);
    }
    printf("convert float->decimal OOM sweep: OK\n");
    bigdecimal_free(&expected);
    bigfloat_free(&a);
}

static void sweep_cpx_float_mul(void) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigcomplex_float_ty a, b, expected;
    bigcomplex_float_init(&a);
    bigcomplex_float_init(&b);
    bigcomplex_float_init(&expected);
    bigcomplex_float_from_str(&a, "1.5+2.5i", &ctx, NULL);
    bigcomplex_float_from_str(&b, "-2.5+3.5i", &ctx, NULL);
    bigcomplex_float_mul(&expected, &a, &b, &ctx);
    for (size_t fail = 1U; fail <= SWEEP_MAX; fail++) {
        bigcomplex_float_ty dst;
        bigcomplex_float_init(&dst);
        bigcomplex_float_copy(&dst, &expected);
        nex_test_alloc_fail_after = fail;
        const bigcomplex_float_err_ty e = bigcomplex_float_mul(&dst, &a, &b,
                &ctx);
        nex_test_alloc_fail_after = SIZE_MAX;
        if (e == BIGCOMPLEX_FLOAT_ERR_OOM_E) {
            CHECK(bigcomplex_float_eq(&dst, &expected));
        } else {
            CHECK(e == BIGCOMPLEX_FLOAT_OK_E);
            CHECK(bigcomplex_float_eq(&dst, &expected));
        }
        bigcomplex_float_free(&dst);
    }
    printf("bigcomplex_float mul OOM sweep: OK\n");
    bigcomplex_float_free(&expected);
    bigcomplex_float_free(&b);
    bigcomplex_float_free(&a);
}

int main(void) {
    sweep_bf_mul();
    sweep_bf_div();
    sweep_bf_sqrt();
    sweep_bf_from_str();
    sweep_bf_to_str();
    sweep_bd_mul();
    sweep_bd_sqrt();
    sweep_frac_add();
    sweep_bin_mul();
    sweep_dec_mul();
    sweep_conv_float_to_dec();
    sweep_cpx_float_mul();

    if (g_fail == 0) {
        printf("test_oom: ALL PASS\n");
        return 0;
    }
    printf("test_oom: %d FAILURES\n", g_fail);
    return 1;
}
