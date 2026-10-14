/*
 * golden_series_driver.c：路线 A 级数求值的黄金对拍驱动（TODO §C4）。
 *
 * 逐行读入命令（stdin），按指定精度求值并输出十进制结果（stdout）：
 *   exp  <prec_bits> <z>
 *   sin  <prec_bits> <z>
 *   cos  <prec_bits> <z>
 *   ln1p <prec_bits> <z>
 *   atan <prec_bits> <z>
 *   exppub <prec_bits> <z>        （bigfloat_exp，正确舍入）
 *   logpub <prec_bits> <z>        （bigfloat_log，正确舍入）
 *   sinpub/cospub/tanpub <prec_bits> <z>
 *   atanpubdec <mant_digits> <z>   （十进制正确舍入）
 *   exppubdec <mant_digits> <z>
 *   logpubdec <mant_digits> <z>
 *   sinpubdec/cospubdec/tanpubdec <mant_digits> <z>
 * 失败输出 "ERR"。参考实现见 golden_series.py（mpmath）。
 */

#include <stdio.h>
#include <string.h>

#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/series/nex_series.h"
#include "nex/transcend/nex_transcend.h"

static bigfloat_ctx_ty g_ctx;
static bigfrac_ty g_eps;

static void setup(unsigned long prec) {
    bigfloat_ctx_make(&g_ctx, (size_t)prec, 20, BIGFLOAT_ROUND_NEAREST_EVEN_E);
    bigint_bin_ty one;
    bigint_bin_ty den;
    bigint_bin_init(&one);
    bigint_bin_init(&den);
    bigint_bin_from_u64(&one, 1U);
    bigint_bin_shl(&den, &one, (size_t)prec + 40U);
    bigfrac_init(&g_eps);
    bigfrac_from_ints(&g_eps, &one, &den);
    bigint_bin_free(&den);
    bigint_bin_free(&one);
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char cmd[32];
        char zstr[512];
        unsigned long prec = 0U;
        if (sscanf(line, "%31s %lu %511s", cmd, &prec, zstr) != 3) {
            continue;
        }
        if (strcmp(cmd, "atanpubdec") == 0 || strcmp(cmd, "exppubdec") == 0 ||
                strcmp(cmd, "logpubdec") == 0 ||
                strcmp(cmd, "sinpubdec") == 0 ||
                strcmp(cmd, "cospubdec") == 0 ||
                strcmp(cmd, "tanpubdec") == 0) {
            bigdecimal_ctx_ty dctx;
            bigdecimal_ty dz;
            bigdecimal_ty dout;
            bigdecimal_init(&dz);
            bigdecimal_init(&dout);
            bigdecimal_err_ty derr = bigdecimal_ctx_make(&dctx, (size_t)prec, 10,
                    BIGDECIMAL_ROUND_NEAREST_EVEN_E);
            if (derr == BIGDECIMAL_OK_E &&
                    bigdecimal_from_str(&dz, zstr, &dctx, NULL) !=
                        BIGDECIMAL_OK_E) {
                derr = BIGDECIMAL_ERR_PARSE_E;
            }
            if (derr == BIGDECIMAL_OK_E) {
                if (strcmp(cmd, "exppubdec") == 0) {
                    derr = bigdecimal_exp(&dout, &dz, &dctx);
                } else if (strcmp(cmd, "logpubdec") == 0) {
                    derr = bigdecimal_log(&dout, &dz, &dctx);
                } else if (strcmp(cmd, "sinpubdec") == 0) {
                    derr = bigdecimal_sin(&dout, &dz, &dctx);
                } else if (strcmp(cmd, "cospubdec") == 0) {
                    derr = bigdecimal_cos(&dout, &dz, &dctx);
                } else if (strcmp(cmd, "tanpubdec") == 0) {
                    derr = bigdecimal_tan(&dout, &dz, &dctx);
                } else {
                    derr = bigdecimal_atan(&dout, &dz, &dctx);
                }
            }
            if (derr == BIGDECIMAL_OK_E) {
                char buf[8192];
                size_t need = 0U;
                if (bigdecimal_to_str(&dout, BIGDECIMAL_FMT_FIXED_E, buf,
                        sizeof(buf), &need) == BIGDECIMAL_OK_E) {
                    printf("%s\n", buf);
                } else {
                    printf("ERR\n");
                }
            } else {
                printf("ERR\n");
            }
            bigdecimal_free(&dout);
            bigdecimal_free(&dz);
            continue;
        }
        setup(prec);

        bigfloat_ty z;
        bigfloat_ty out;
        bigfloat_init(&z);
        bigfloat_init(&out);
        bigfloat_err_ty err = BIGFLOAT_ERR_INVALID_E;
        if (bigfloat_from_str(&z, zstr, &g_ctx, NULL) == BIGFLOAT_OK_E) {
            if (strcmp(cmd, "exp") == 0) {
                err = nex_series_exp_eval_float(&out, NULL, &z, &g_eps, &g_ctx);
            } else if (strcmp(cmd, "sin") == 0) {
                err = nex_series_sin_eval_float(&out, NULL, &z, &g_eps, &g_ctx);
            } else if (strcmp(cmd, "cos") == 0) {
                err = nex_series_cos_eval_float(&out, NULL, &z, &g_eps, &g_ctx);
            } else if (strcmp(cmd, "ln1p") == 0) {
                err = nex_series_ln1p_eval_float(&out, NULL, &z, &g_eps, &g_ctx);
            } else if (strcmp(cmd, "atan") == 0) {
                err = nex_series_atan_eval_float(&out, NULL, &z, &g_eps, &g_ctx);
            } else if (strcmp(cmd, "atanpub") == 0) {
                err = bigfloat_atan(&out, &z, &g_ctx);
            } else if (strcmp(cmd, "exppub") == 0) {
                err = bigfloat_exp(&out, &z, &g_ctx);
            } else if (strcmp(cmd, "logpub") == 0) {
                err = bigfloat_log(&out, &z, &g_ctx);
            } else if (strcmp(cmd, "sinpub") == 0) {
                err = bigfloat_sin(&out, &z, &g_ctx);
            } else if (strcmp(cmd, "cospub") == 0) {
                err = bigfloat_cos(&out, &z, &g_ctx);
            } else if (strcmp(cmd, "tanpub") == 0) {
                err = bigfloat_tan(&out, &z, &g_ctx);
            }
        }

        if (err == BIGFLOAT_OK_E) {
            char buf[8192];
            size_t need = 0U;
            if (bigfloat_to_str(&out, 0U, buf, sizeof(buf), &need) ==
                    BIGFLOAT_OK_E) {
                printf("%s\n", buf);
            } else {
                printf("ERR\n");
            }
        } else {
            printf("ERR\n");
        }
        bigfloat_free(&out);
        bigfloat_free(&z);
        bigfrac_free(&g_eps);
    }
    return 0;
}
