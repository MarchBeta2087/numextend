/* 黄金对拍驱动（bigdecimal）：行式协议，stdin 读命令，stdout 输出结果。
 *
 * 数值表示：<flag> <mant> <exp>（flag：0=+0 1=−0 2=+正常 3=−正常
 * 4=+inf 5=−inf 6=nan；特殊值时 mant=0 exp=0；mant 为十进制整数）。
 *
 * 协议（空格分隔）：
 *   bd_ctx <mant_digits> <exp_digits> <round>  → R ok（设定当前上下文）
 *   bd_parse <decstr>                          → R <flag> <mant> <exp> | E parse
 *   bd_add|bd_sub|bd_mul|bd_div <a> <b>        → R <flag> <mant> <exp>
 *   bd_sqrt <a>                                → R <flag> <mant> <exp>
 *   bd_cmp <a> <b>                             → R -1|0|1|2（2 = NaN 语义）
 *   bd_frombigint <decint>                     → R <flag> <mant> <exp>
 *   bd_tstr <fmt> <flag> <mant> <exp>          → R <string>（fmt 0=定点 1=科学）
 *   bd_neg|bd_comp|bd_decomp <flag> <mant> <exp> → R <flag> <mant> <exp>
 * 输入以 EOF 结束；错误命令输出 E <msg>。
 */
#include "nex/bigdecimal/nex_bigdecimal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[65536];
static bigdecimal_ctx_ty g_ctx;

static void print_value(const bigdecimal_ty *v) {
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

static int parse_value(bigdecimal_ty *v, const char *flag_s,
        const char *mant_s, const char *exp_s) {
    if ((flag_s == NULL) || (mant_s == NULL) || (exp_s == NULL)) {
        return -1;
    }
    const int flag = atoi(flag_s);
    if ((flag < 0) || (flag > 6)) {
        return -1;
    }
    bigdecimal_init(v);
    v->flag = (bigdecimal_flag_ty)flag;
    v->exp = strtoll(exp_s, NULL, 10);
    if (bigint_dec_from_str(&v->mant, mant_s, 10, NULL) != BIGINT_OK_E) {
        bigdecimal_free(v);
        return -1;
    }
    return 0;
}

static void binary_op(const char *op, const char *a_str, const char *b_str) {
    bigdecimal_ty x;
    bigdecimal_ty y;
    bigdecimal_ty z;
    bigdecimal_init(&x);
    bigdecimal_init(&y);
    bigdecimal_init(&z);
    if (bigdecimal_from_str(&x, a_str, &g_ctx, NULL) != BIGDECIMAL_OK_E) {
        printf("E parse\n");
    } else if ((b_str != NULL)
            && (bigdecimal_from_str(&y, b_str, &g_ctx, NULL)
                    != BIGDECIMAL_OK_E)) {
        printf("E parse\n");
    } else {
        bigdecimal_err_ty rc = BIGDECIMAL_ERR_INVALID_E;
        if (strcmp(op, "add") == 0) {
            rc = bigdecimal_add(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "sub") == 0) {
            rc = bigdecimal_sub(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "mul") == 0) {
            rc = bigdecimal_mul(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "div") == 0) {
            rc = bigdecimal_div(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "cmp") == 0) {
            printf("R %d\n", bigdecimal_cmp(&x, &y));
        } else if (strcmp(op, "sqrt") == 0) {
            rc = bigdecimal_sqrt(&z, &x, &g_ctx);
        }
        if (strcmp(op, "cmp") != 0) {
            if (rc != BIGDECIMAL_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_value(&z);
                printf("\n");
            }
        }
    }
    bigdecimal_free(&z);
    bigdecimal_free(&y);
    bigdecimal_free(&x);
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
        char *p3 = strtok(NULL, " ");
        char *p4 = strtok(NULL, " ");

        if (strcmp(op, "bd_ctx") == 0) {
            if ((p1 == NULL) || (p2 == NULL) || (p3 == NULL)) {
                printf("E arg\n");
            } else if (bigdecimal_ctx_make(&g_ctx, (size_t)atoll(p1),
                    (size_t)atoll(p2), (bigdecimal_round_ty)atoi(p3))
                    != BIGDECIMAL_OK_E) {
                printf("E arg\n");
            } else {
                printf("R ok\n");
            }
        } else if (strcmp(op, "bd_parse") == 0) {
            bigdecimal_ty v;
            bigdecimal_init(&v);
            if (bigdecimal_from_str(&v, p1, &g_ctx, NULL) != BIGDECIMAL_OK_E) {
                printf("E parse\n");
            } else {
                printf("R ");
                print_value(&v);
                printf("\n");
            }
            bigdecimal_free(&v);
        } else if ((strcmp(op, "bd_add") == 0) || (strcmp(op, "bd_sub") == 0)
                || (strcmp(op, "bd_mul") == 0) || (strcmp(op, "bd_div") == 0)
                || (strcmp(op, "bd_sqrt") == 0) || (strcmp(op, "bd_cmp") == 0)) {
            binary_op(op + 3, p1, p2);
        } else if (strcmp(op, "bd_frombigint") == 0) {
            bigdecimal_ty v;
            bigint_dec_ty n;
            bigdecimal_init(&v);
            bigint_dec_init(&n);
            if ((p1 == NULL)
                    || (bigint_dec_from_str(&n, p1, 10, NULL) != BIGINT_OK_E)) {
                printf("E arg\n");
            } else if (bigdecimal_from_bigint(&v, &n, &g_ctx)
                    != BIGDECIMAL_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_value(&v);
                printf("\n");
            }
            bigint_dec_free(&n);
            bigdecimal_free(&v);
        } else if (strcmp(op, "bd_tstr") == 0) {
            bigdecimal_ty v;
            if (parse_value(&v, p2, p3, p4) != 0) {
                printf("E arg\n");
            } else {
                char buf[8192];
                const bigdecimal_err_ty rc = bigdecimal_to_str(&v,
                        (bigdecimal_fmt_ty)atoi(p1), buf, sizeof(buf), NULL);
                if (rc != BIGDECIMAL_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R %s\n", buf);
                }
                bigdecimal_free(&v);
            }
        } else if ((strcmp(op, "bd_neg") == 0)
                || (strcmp(op, "bd_comp") == 0)
                || (strcmp(op, "bd_decomp") == 0)) {
            bigdecimal_ty v;
            bigdecimal_ty w;
            if (parse_value(&v, p1, p2, p3) != 0) {
                printf("E arg\n");
                continue;
            }
            bigdecimal_init(&w);
            if (strcmp(op, "bd_neg") == 0) {
                bigdecimal_neg(&v);
                printf("R ");
                print_value(&v);
                printf("\n");
            } else if (strcmp(op, "bd_comp") == 0) {
                /* compose 接受全部七标志：特殊值忽略 mant/exp */
                bigdecimal_compose(&w, &v.mant, v.exp, v.flag, &g_ctx);
                printf("R ");
                print_value(&w);
                printf("\n");
            } else {
                printf("R ");
                print_value(&v);
                printf("\n");
            }
            bigdecimal_free(&w);
            bigdecimal_free(&v);
        } else {
            printf("E unknown\n");
        }
    }
    return 0;
}
