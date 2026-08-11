/* 黄金对拍驱动（bigfloat）：行式协议，stdin 读命令，stdout 输出结果。
 *
 * 数值表示：<flag> <mant> <exp>（flag：0=+0 1=−0 2=+正常 3=−正常
 * 4=+inf 5=−inf 6=nan；特殊值时 mant=0 exp=0）。
 *
 * 协议（空格分隔）：
 *   bf_ctx <mant_bits> <exp_bits> <round>   → R ok（设定当前上下文）
 *   bf_parse <decstr>                       → R <flag> <mant> <exp> | E parse
 *   bf_add|bf_sub|bf_mul|bf_div <a> <b>     → R <flag> <mant> <exp>（操作数为十进制串）
 *   bf_sqrt <a>                             → R <flag> <mant> <exp>
 *   bf_cmp <a> <b>                          → R -1|0|1|2（2 = NaN 语义）
 *   bf_f64 <hex64>                          → R <flag> <mant> <exp>
 *   bf_tf64 <flag> <mant> <exp>             → R <hex64> <errcode>（errcode 0=ok 1=overflow）
 *   bf_tstr <flag> <mant> <exp>             → R <string>
 *   bf_neg <flag> <mant> <exp>              → R <flag> <mant> <exp>
 *   bf_comp <flag> <mant> <exp>             → R <flag> <mant> <exp>（compose 规范化）
 *   bf_decomp <flag> <mant> <exp>           → R <flag> <mant> <exp>（恒等）
 * 输入以 EOF 结束；错误命令输出 E <msg>。
 */
#include "nex/bigfloat/nex_bigfloat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[65536];
static bigfloat_ctx_ty g_ctx;

static void print_value(const bigfloat_ty *v) {
    char buf[4096];
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

static int parse_value(bigfloat_ty *v, const char *flag_s, const char *mant_s,
        const char *exp_s) {
    if ((flag_s == NULL) || (mant_s == NULL) || (exp_s == NULL)) {
        return -1;
    }
    const int flag = atoi(flag_s);
    if ((flag < 0) || (flag > 6)) {
        return -1;
    }
    bigfloat_init(v);
    v->flag = (bigfloat_flag_ty)flag;
    v->exp = strtoll(exp_s, NULL, 10);
    if (bigint_bin_from_str(&v->mant, mant_s, 10, NULL) != BIGINT_OK_E) {
        bigfloat_free(v);
        return -1;
    }
    return 0;
}

static void parse_op(const char *op, const char *a_str, const char *b_str) {
    bigfloat_ty x;
    bigfloat_ty y;
    bigfloat_ty z;
    bigfloat_init(&x);
    bigfloat_init(&y);
    bigfloat_init(&z);
    if (bigfloat_from_str(&x, a_str, &g_ctx, NULL) != BIGFLOAT_OK_E) {
        printf("E parse\n");
    } else if ((b_str != NULL)
            && (bigfloat_from_str(&y, b_str, &g_ctx, NULL) != BIGFLOAT_OK_E)) {
        printf("E parse\n");
    } else {
        bigfloat_err_ty rc = BIGFLOAT_ERR_INVALID_E;
        if (strcmp(op, "add") == 0) {
            rc = bigfloat_add(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "sub") == 0) {
            rc = bigfloat_sub(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "mul") == 0) {
            rc = bigfloat_mul(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "div") == 0) {
            rc = bigfloat_div(&z, &x, &y, &g_ctx);
        } else if (strcmp(op, "cmp") == 0) {
            printf("R %d\n", bigfloat_cmp(&x, &y));
        } else if (strcmp(op, "sqrt") == 0) {
            rc = bigfloat_sqrt(&z, &x, &g_ctx);
        }
        if (strcmp(op, "cmp") != 0) {
            if (rc != BIGFLOAT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_value(&z);
                printf("\n");
            }
        }
    }
    bigfloat_free(&z);
    bigfloat_free(&y);
    bigfloat_free(&x);
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

        if (strcmp(op, "bf_ctx") == 0) {
            if ((p1 == NULL) || (p2 == NULL) || (p3 == NULL)) {
                printf("E arg\n");
            } else if (bigfloat_ctx_make(&g_ctx, (size_t)atoll(p1),
                    (size_t)atoll(p2), (bigfloat_round_ty)atoi(p3))
                    != BIGFLOAT_OK_E) {
                printf("E arg\n");
            } else {
                printf("R ok\n");
            }
        } else if (strcmp(op, "bf_parse") == 0) {
            bigfloat_ty v;
            bigfloat_init(&v);
            if (bigfloat_from_str(&v, p1, &g_ctx, NULL) != BIGFLOAT_OK_E) {
                printf("E parse\n");
            } else {
                printf("R ");
                print_value(&v);
                printf("\n");
            }
            bigfloat_free(&v);
        } else if ((strcmp(op, "bf_add") == 0) || (strcmp(op, "bf_sub") == 0)
                || (strcmp(op, "bf_mul") == 0) || (strcmp(op, "bf_div") == 0)
                || (strcmp(op, "bf_sqrt") == 0) || (strcmp(op, "bf_cmp") == 0)) {
            parse_op(op + 3, p1, p2);
        } else if (strcmp(op, "bf_f64") == 0) {
            bigfloat_ty v;
            bigfloat_init(&v);
            /* 按位模式构造 double（字节序无关） */
            uint64_t bits = (uint64_t)strtoull(p1, NULL, 16);
            double d;
            memcpy(&d, &bits, sizeof(d));
            bigfloat_from_f64(&v, d);
            printf("R ");
            print_value(&v);
            printf("\n");
            bigfloat_free(&v);
        } else if (strcmp(op, "bf_tf64") == 0) {
            bigfloat_ty v;
            if (parse_value(&v, p1, p2, p3) != 0) {
                printf("E arg\n");
            } else {
                double out = 0.0;
                const bigfloat_err_ty rc = bigfloat_to_f64(&v, &out);
                uint64_t bits;
                memcpy(&bits, &out, sizeof(bits));
                printf("R %016llx %d\n", (unsigned long long)bits,
                        (rc == BIGFLOAT_ERR_OVERFLOW_E) ? 1 : 0);
                bigfloat_free(&v);
            }
        } else if (strcmp(op, "bf_tstr") == 0) {
            bigfloat_ty v;
            if (parse_value(&v, p1, p2, p3) != 0) {
                printf("E arg\n");
            } else {
                char buf[8192];
                const bigfloat_err_ty rc = bigfloat_to_str(&v, 0, buf,
                        sizeof(buf), NULL);
                if (rc != BIGFLOAT_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R %s\n", buf);
                }
                bigfloat_free(&v);
            }
        } else if ((strcmp(op, "bf_neg") == 0)
                || (strcmp(op, "bf_comp") == 0)
                || (strcmp(op, "bf_decomp") == 0)) {
            bigfloat_ty v;
            bigfloat_ty w;
            if (parse_value(&v, p1, p2, p3) != 0) {
                printf("E arg\n");
                continue;
            }
            bigfloat_init(&w);
            if (strcmp(op, "bf_neg") == 0) {
                bigfloat_neg(&v);
                printf("R ");
                print_value(&v);
                printf("\n");
            } else if (strcmp(op, "bf_comp") == 0) {
                /* compose 接受全部七标志：特殊值忽略 mant/exp */
                bigfloat_compose(&w, &v.mant, v.exp, v.flag, &g_ctx);
                printf("R ");
                print_value(&w);
                printf("\n");
            } else {
                printf("R ");
                print_value(&v);
                printf("\n");
            }
            bigfloat_free(&w);
            bigfloat_free(&v);
        } else {
            printf("E unknown\n");
        }
    }
    return 0;
}
