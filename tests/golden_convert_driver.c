/* 黄金对拍驱动（nex_convert）：行式协议，stdin 读命令，stdout 输出结果。
 *
 * 数值表示：浮点 <flag> <mant> <exp>（bigfloat / bigdecimal 规范值）；
 * 分数 <num> <den>（bigint_bin 十进制串）；整数 <串>。
 *
 * 协议（空格分隔）：
 *   cv_ctx_bf <mant_bits> <exp_bits> <round>   → R ok（设定 bigfloat ctx）
 *   cv_ctx_bd <mant_digits> <exp_digits> <round> → R ok（设定 bigdecimal ctx）
 *   cv_float_to_decimal <floatstr>              → R <dec 值>
 *   cv_decimal_to_float <decstr>                → R <bf 值>
 *   cv_frac_to_float <num> <den>                → R <bf 值>
 *   cv_frac_to_decimal <num> <den>              → R <dec 值>
 *   cv_dec_to_float <decint>                    → R <bf 值>
 *   cv_float_to_frac <floatstr>                 → R <num> <den> | E invalid
 *   cv_decimal_to_frac <decstr>                 → R <num> <den> | E invalid
 *   cv_float_to_bin <floatstr>                  → R <int> | E invalid
 *   cv_decimal_to_bin <decstr>                  → R <int> | E invalid
 *   cv_cpx_float_to_float <complexstr>          → R <bf 值> | E invalid
 *   cv_cpx_decimal_to_decimal <complexstr>      → R <dec 值> | E invalid
 * 输入以 EOF 结束；错误命令输出 E <msg>。
 */
#include "nex/convert/nex_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[65536];
static bigfloat_ctx_ty g_bfctx;
static bigdecimal_ctx_ty g_bdctx;

static void print_bf(const bigfloat_ty *v) {
    size_t needed = 0;
    if (bigint_bin_to_str(&v->mant, 10, NULL, 0, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        return;
    }
    char *mbuf = (char *)malloc(needed);
    if (mbuf == NULL) {
        printf("E oom\n");
        return;
    }
    if (bigint_bin_to_str(&v->mant, 10, mbuf, needed, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        free(mbuf);
        return;
    }
    printf("%d %s %lld", (int)v->flag, mbuf, (long long)v->exp);
    free(mbuf);
}

static void print_bd(const bigdecimal_ty *v) {
    size_t needed = 0;
    if (bigint_dec_to_str(&v->mant, 10, NULL, 0, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        return;
    }
    char *mbuf = (char *)malloc(needed);
    if (mbuf == NULL) {
        printf("E oom\n");
        return;
    }
    if (bigint_dec_to_str(&v->mant, 10, mbuf, needed, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        free(mbuf);
        return;
    }
    printf("%d %s %lld", (int)v->flag, mbuf, (long long)v->exp);
    free(mbuf);
}

static int parse_bf(bigfloat_ty *v, const char *s) {
    return (bigfloat_from_str(v, s, &g_bfctx, NULL) == BIGFLOAT_OK_E) ? 0 : -1;
}

static int parse_bd(bigdecimal_ty *v, const char *s) {
    return (bigdecimal_from_str(v, s, &g_bdctx, NULL) == BIGDECIMAL_OK_E)
            ? 0 : -1;
}

int main(void) {
    while (fgets(g_buf, sizeof(g_buf), stdin) != NULL) {
        size_t len = strlen(g_buf);
        while ((len > 0U)
                && ((g_buf[len - 1U] == '\n') || (g_buf[len - 1U] == '\r'))) {
            g_buf[--len] = '\0';
        }
        if (len == 0U) {
            continue;
        }
        char op[32];
        char *tok = strtok(g_buf, " ");
        if (tok == NULL) {
            printf("E empty\n");
            continue;
        }
        snprintf(op, sizeof(op), "%s", tok);
        char *p1 = strtok(NULL, " ");
        char *p2 = strtok(NULL, " ");
        char *p3 = strtok(NULL, " ");

        if (strcmp(op, "cv_ctx_bf") == 0) {
            if (bigfloat_ctx_make(&g_bfctx, (size_t)atoll(p1),
                    (size_t)atoll(p2), (bigfloat_round_ty)atoi(p3))
                    != BIGFLOAT_OK_E) {
                printf("E arg\n");
            } else {
                printf("R ok\n");
            }
        } else if (strcmp(op, "cv_ctx_bd") == 0) {
            if (bigdecimal_ctx_make(&g_bdctx, (size_t)atoll(p1),
                    (size_t)atoll(p2), (bigdecimal_round_ty)atoi(p3))
                    != BIGDECIMAL_OK_E) {
                printf("E arg\n");
            } else {
                printf("R ok\n");
            }
        } else if (strcmp(op, "cv_float_to_decimal") == 0) {
            bigfloat_ty a;
            bigdecimal_ty b;
            bigfloat_init(&a);
            bigdecimal_init(&b);
            if (parse_bf(&a, p1) != 0) {
                printf("E parse\n");
            } else if (nex_convert_float_to_decimal(&b, &a, &g_bdctx)
                    != NEX_CONVERT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_bd(&b);
                printf("\n");
            }
            bigdecimal_free(&b);
            bigfloat_free(&a);
        } else if (strcmp(op, "cv_decimal_to_float") == 0) {
            bigdecimal_ty a;
            bigfloat_ty b;
            bigdecimal_init(&a);
            bigfloat_init(&b);
            if (parse_bd(&a, p1) != 0) {
                printf("E parse\n");
            } else if (nex_convert_decimal_to_float(&b, &a, &g_bfctx)
                    != NEX_CONVERT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_bf(&b);
                printf("\n");
            }
            bigfloat_free(&b);
            bigdecimal_free(&a);
        } else if (strcmp(op, "cv_frac_to_float") == 0) {
            bigint_bin_ty n;
            bigint_bin_ty d;
            bigfrac_ty f;
            bigfloat_ty b;
            bigint_bin_init(&n);
            bigint_bin_init(&d);
            bigfrac_init(&f);
            bigfloat_init(&b);
            if ((bigint_bin_from_str(&n, p1, 10, NULL) != BIGINT_OK_E)
                    || (bigint_bin_from_str(&d, p2, 10, NULL)
                            != BIGINT_OK_E)) {
                printf("E parse\n");
            } else if (bigfrac_from_ints(&f, &n, &d) != BIGFRAC_OK_E) {
                printf("E err\n");
            } else if (nex_convert_frac_to_float(&b, &f, &g_bfctx)
                    != NEX_CONVERT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_bf(&b);
                printf("\n");
            }
            bigfloat_free(&b);
            bigfrac_free(&f);
            bigint_bin_free(&d);
            bigint_bin_free(&n);
        } else if (strcmp(op, "cv_frac_to_decimal") == 0) {
            bigint_bin_ty n;
            bigint_bin_ty d;
            bigfrac_ty f;
            bigdecimal_ty b;
            bigint_bin_init(&n);
            bigint_bin_init(&d);
            bigfrac_init(&f);
            bigdecimal_init(&b);
            if ((bigint_bin_from_str(&n, p1, 10, NULL) != BIGINT_OK_E)
                    || (bigint_bin_from_str(&d, p2, 10, NULL)
                            != BIGINT_OK_E)) {
                printf("E parse\n");
            } else if (bigfrac_from_ints(&f, &n, &d) != BIGFRAC_OK_E) {
                printf("E err\n");
            } else if (nex_convert_frac_to_decimal(&b, &f, &g_bdctx)
                    != NEX_CONVERT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_bd(&b);
                printf("\n");
            }
            bigdecimal_free(&b);
            bigfrac_free(&f);
            bigint_bin_free(&d);
            bigint_bin_free(&n);
        } else if (strcmp(op, "cv_dec_to_float") == 0) {
            bigint_dec_ty a;
            bigfloat_ty b;
            bigint_dec_init(&a);
            bigfloat_init(&b);
            if (bigint_dec_from_str(&a, p1, 10, NULL) != BIGINT_OK_E) {
                printf("E parse\n");
            } else if (nex_convert_dec_to_float(&b, &a, &g_bfctx)
                    != NEX_CONVERT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_bf(&b);
                printf("\n");
            }
            bigfloat_free(&b);
            bigint_dec_free(&a);
        } else if (strcmp(op, "cv_float_to_frac") == 0) {
            bigfloat_ty a;
            bigfrac_ty f;
            bigfloat_init(&a);
            bigfrac_init(&f);
            if (parse_bf(&a, p1) != 0) {
                printf("E parse\n");
            } else if (nex_convert_float_to_frac(&f, &a)
                    != NEX_CONVERT_OK_E) {
                printf("E invalid\n");
            } else {
                size_t nlen = 0, dlen = 0;
                bigint_bin_to_str(&f.num, 10, NULL, 0, &nlen);
                bigint_bin_to_str(&f.den, 10, NULL, 0, &dlen);
                char *nb = (char *)malloc(nlen), *db = (char *)malloc(dlen);
                if ((nb == NULL) || (db == NULL)) {
                    printf("E oom\n");
                } else {
                    bigint_bin_to_str(&f.num, 10, nb, nlen, &nlen);
                    bigint_bin_to_str(&f.den, 10, db, dlen, &dlen);
                    printf("R %s %s\n", nb, db);
                    free(nb);
                    free(db);
                }
            }
            bigfrac_free(&f);
            bigfloat_free(&a);
        } else if (strcmp(op, "cv_decimal_to_frac") == 0) {
            bigdecimal_ty a;
            bigfrac_ty f;
            bigdecimal_init(&a);
            bigfrac_init(&f);
            if (parse_bd(&a, p1) != 0) {
                printf("E parse\n");
            } else if (nex_convert_decimal_to_frac(&f, &a)
                    != NEX_CONVERT_OK_E) {
                printf("E invalid\n");
            } else {
                size_t nlen = 0, dlen = 0;
                bigint_bin_to_str(&f.num, 10, NULL, 0, &nlen);
                bigint_bin_to_str(&f.den, 10, NULL, 0, &dlen);
                char *nb = (char *)malloc(nlen), *db = (char *)malloc(dlen);
                if ((nb == NULL) || (db == NULL)) {
                    printf("E oom\n");
                } else {
                    bigint_bin_to_str(&f.num, 10, nb, nlen, &nlen);
                    bigint_bin_to_str(&f.den, 10, db, dlen, &dlen);
                    printf("R %s %s\n", nb, db);
                    free(nb);
                    free(db);
                }
            }
            bigfrac_free(&f);
            bigdecimal_free(&a);
        } else if (strcmp(op, "cv_float_to_bin") == 0) {
            bigfloat_ty a;
            bigint_bin_ty b;
            bigfloat_init(&a);
            bigint_bin_init(&b);
            if (parse_bf(&a, p1) != 0) {
                printf("E parse\n");
            } else if (nex_convert_float_to_bin(&b, &a) != NEX_CONVERT_OK_E) {
                printf("E invalid\n");
            } else {
                size_t need = 0;
                bigint_bin_to_str(&b, 10, NULL, 0, &need);
                char *buf = (char *)malloc(need);
                if (buf == NULL) {
                    printf("E oom\n");
                } else {
                    bigint_bin_to_str(&b, 10, buf, need, &need);
                    printf("R %s\n", buf);
                    free(buf);
                }
            }
            bigint_bin_free(&b);
            bigfloat_free(&a);
        } else if (strcmp(op, "cv_decimal_to_bin") == 0) {
            bigdecimal_ty a;
            bigint_bin_ty b;
            bigdecimal_init(&a);
            bigint_bin_init(&b);
            if (parse_bd(&a, p1) != 0) {
                printf("E parse\n");
            } else if (nex_convert_decimal_to_bin(&b, &a) != NEX_CONVERT_OK_E) {
                printf("E invalid\n");
            } else {
                size_t need = 0;
                bigint_bin_to_str(&b, 10, NULL, 0, &need);
                char *buf = (char *)malloc(need);
                if (buf == NULL) {
                    printf("E oom\n");
                } else {
                    bigint_bin_to_str(&b, 10, buf, need, &need);
                    printf("R %s\n", buf);
                    free(buf);
                }
            }
            bigint_bin_free(&b);
            bigdecimal_free(&a);
        } else if (strcmp(op, "cv_cpx_float_to_float") == 0) {
            bigcomplex_float_ty z;
            bigfloat_ty b;
            bigcomplex_float_init(&z);
            bigfloat_init(&b);
            if (bigcomplex_float_from_str(&z, p1, &g_bfctx, NULL)
                    != BIGCOMPLEX_FLOAT_OK_E) {
                printf("E parse\n");
            } else if (nex_convert_cpx_float_to_float(&b, &z)
                    != NEX_CONVERT_OK_E) {
                printf("E invalid\n");
            } else {
                printf("R ");
                print_bf(&b);
                printf("\n");
            }
            bigfloat_free(&b);
            bigcomplex_float_free(&z);
        } else if (strcmp(op, "cv_cpx_decimal_to_decimal") == 0) {
            bigcomplex_decimal_ty z;
            bigdecimal_ty b;
            bigcomplex_decimal_init(&z);
            bigdecimal_init(&b);
            if (bigcomplex_decimal_from_str(&z, p1, &g_bdctx, NULL)
                    != BIGCOMPLEX_DECIMAL_OK_E) {
                printf("E parse\n");
            } else if (nex_convert_cpx_decimal_to_decimal(&b, &z)
                    != NEX_CONVERT_OK_E) {
                printf("E invalid\n");
            } else {
                printf("R ");
                print_bd(&b);
                printf("\n");
            }
            bigdecimal_free(&b);
            bigcomplex_decimal_free(&z);
        } else {
            printf("E unknown\n");
        }
    }
    return 0;
}
