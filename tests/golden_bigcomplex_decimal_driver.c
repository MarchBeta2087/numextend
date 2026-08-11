/* 黄金对拍驱动（bigcomplex decimal 版）：行式协议，stdin 读命令，
 * stdout 输出结果。与 float 版平行，分量上下文为 bigdecimal_ctx_ty。
 *
 * 复数表示：<re_flag> <re_mant> <re_exp> <im_flag> <im_mant> <im_exp>
 * （分量为 bigdecimal 规范值；flag 见 bigdecimal_flag_ty）。
 *
 * 协议（空格分隔）：
 *   bcd_ctx <mant_digits> <exp_digits> <round>   → R ok
 *   bcd_parse <str>                              → R <6 值> | E parse
 *   bcd_add|bcd_sub|bcd_mul|bcd_div <a> <b>      → R <6 值>
 *   bcd_abs <a>                                  → R <flag> <mant> <exp>
 *   bcd_conj <a>                                 → R <6 值>
 *   bcd_eq <a> <b>                               → R 0|1
 *   bcd_tstr <fmt> <str>                         → R <string>
 * 输入以 EOF 结束；错误命令输出 E <msg>。
 */
#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[65536];
static bigdecimal_ctx_ty g_ctx;

static void print_scalar(const bigdecimal_ty *v) {
    char buf[4096];
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

static void print_value(const bigcomplex_decimal_ty *z) {
    print_scalar(&z->re);
    printf(" ");
    print_scalar(&z->im);
    printf("\n");
}

static int parse_value(bigcomplex_decimal_ty *z, const char *s) {
    return (bigcomplex_decimal_from_str(z, s, &g_ctx, NULL)
            == BIGCOMPLEX_DECIMAL_OK_E) ? 0 : -1;
}

static void binop(const char *op, const char *a_str, const char *b_str) {
    bigcomplex_decimal_ty x;
    bigcomplex_decimal_ty y;
    bigcomplex_decimal_ty z;
    bigcomplex_decimal_init(&x);
    bigcomplex_decimal_init(&y);
    bigcomplex_decimal_init(&z);
    if ((parse_value(&x, a_str) != 0)
            || ((b_str != NULL) && (parse_value(&y, b_str) != 0))) {
        printf("E parse\n");
    } else if (strcmp(op, "eq") == 0) {
        printf("R %d\n", bigcomplex_decimal_eq(&x, &y) ? 1 : 0);
    } else {
        bigcomplex_decimal_err_ty rc = BIGCOMPLEX_DECIMAL_ERR_INVALID_E;
        if (strcmp(op, "add") == 0) {
            rc = bigcomplex_decimal_add(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "sub") == 0) {
            rc = bigcomplex_decimal_sub(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "mul") == 0) {
            rc = bigcomplex_decimal_mul(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "div") == 0) {
            rc = bigcomplex_decimal_div(&z, &x, &y, &g_ctx);
        }
        if (rc != BIGCOMPLEX_DECIMAL_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_value(&z);
        }
    }
    bigcomplex_decimal_free(&z);
    bigcomplex_decimal_free(&y);
    bigcomplex_decimal_free(&x);
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
        char op[16];
        char *tok = strtok(g_buf, " ");
        if (tok == NULL) {
            printf("E empty\n");
            continue;
        }
        snprintf(op, sizeof(op), "%s", tok);
        char *p1 = strtok(NULL, " ");
        char *p2 = strtok(NULL, " ");

        if (strcmp(op, "bcd_ctx") == 0) {
            char *p3 = strtok(NULL, " ");
            if ((p1 == NULL) || (p2 == NULL) || (p3 == NULL)) {
                printf("E arg\n");
            } else if (bigdecimal_ctx_make(&g_ctx, (size_t)atoll(p1),
                    (size_t)atoll(p2), (bigdecimal_round_ty)atoi(p3))
                    != BIGDECIMAL_OK_E) {
                printf("E arg\n");
            } else {
                printf("R ok\n");
            }
        } else if (strcmp(op, "bcd_parse") == 0) {
            bigcomplex_decimal_ty z;
            bigcomplex_decimal_init(&z);
            if (parse_value(&z, p1) != 0) {
                printf("E parse\n");
            } else {
                printf("R ");
                print_value(&z);
            }
            bigcomplex_decimal_free(&z);
        } else if ((strcmp(op, "bcd_add") == 0) || (strcmp(op, "bcd_sub") == 0)
                || (strcmp(op, "bcd_mul") == 0) || (strcmp(op, "bcd_div") == 0)
                || (strcmp(op, "bcd_eq") == 0)) {
            binop(op + 4, p1, p2);
        } else if (strcmp(op, "bcd_abs") == 0) {
            bigcomplex_decimal_ty z;
            bigdecimal_ty v;
            bigcomplex_decimal_init(&z);
            bigdecimal_init(&v);
            if (parse_value(&z, p1) != 0) {
                printf("E parse\n");
            } else if (bigcomplex_decimal_abs(&v, &z, &g_ctx)
                    != BIGCOMPLEX_DECIMAL_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_scalar(&v);
                printf("\n");
            }
            bigdecimal_free(&v);
            bigcomplex_decimal_free(&z);
        } else if (strcmp(op, "bcd_conj") == 0) {
            bigcomplex_decimal_ty z;
            bigcomplex_decimal_init(&z);
            if (parse_value(&z, p1) != 0) {
                printf("E parse\n");
            } else if (bigcomplex_decimal_conj(&z) != BIGCOMPLEX_DECIMAL_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_value(&z);
            }
            bigcomplex_decimal_free(&z);
        } else if (strcmp(op, "bcd_tstr") == 0) {
            bigcomplex_decimal_ty z;
            bigcomplex_decimal_init(&z);
            if (parse_value(&z, p2) != 0) {
                printf("E parse\n");
            } else {
                char buf[8192];
                const bigcomplex_decimal_err_ty rc =
                        bigcomplex_decimal_to_str(&z,
                                (bigdecimal_fmt_ty)atoi(p1), buf,
                                sizeof(buf), NULL);
                if (rc != BIGCOMPLEX_DECIMAL_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R %s\n", buf);
                }
            }
            bigcomplex_decimal_free(&z);
        } else {
            printf("E unknown\n");
        }
    }
    return 0;
}
