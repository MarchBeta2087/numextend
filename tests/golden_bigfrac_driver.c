/* 黄金对拍驱动（bigfrac）：行式协议，stdin 读命令，stdout 输出结果。
 *
 * 协议（空格分隔；分数操作数以 "p/q"、"p" 或十进制字符串给出）：
 *   f_id a            → R num den      （解析-输出往返）
 *   f_add a b         → R num den
 *   f_sub a b         → R num den
 *   f_mul a b         → R num den
 *   f_div a b         → R num den      （b == 0 → E divzero）
 *   f_neg a           → R num den
 *   f_inv a           → R num den      （a == 0 → E divzero）
 *   f_cmp a b         → R (-1|0|1)
 *   f_fromints n d    → R num den      （任意分子分母，d == 0 → E divzero）
 *   f_fromstr s       → R num den      （s 为单个 token；解析失败 → E parse，
 *                                        "p/0" → E divzero）
 * 输入以 EOF 结束；错误命令输出 E <msg>。
 */
#include "nex/bigfrac/nex_bigfrac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[65536];

static void print_bigint(const bigint_bin_ty *v) {
    size_t needed = 0;
    if (bigint_bin_to_str(v, 10, NULL, 0, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        return;
    }
    char *out = (char *)malloc(needed);
    if (out == NULL) {
        printf("E oom\n");
        return;
    }
    if (bigint_bin_to_str(v, 10, out, needed, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        free(out);
        return;
    }
    printf("%s", out);
    free(out);
}

static void print_frac(const bigfrac_ty *f) {
    print_bigint(&f->num);
    printf(" ");
    print_bigint(&f->den);
}

static int parse_frac(bigfrac_ty *f, const char *s) {
    if (s == NULL) {
        return -1;
    }
    return (bigfrac_from_str(f, s, NULL) == BIGFRAC_OK_E) ? 0 : -1;
}

static void frac_binary(const char *op, const char *a_str, const char *b_str) {
    bigfrac_ty x;
    bigfrac_ty y;
    bigfrac_ty z;
    bigfrac_init(&x);
    bigfrac_init(&y);
    bigfrac_init(&z);

    if ((parse_frac(&x, a_str) != 0) || (parse_frac(&y, b_str) != 0)) {
        printf("E parse\n");
    } else if (strcmp(op, "cmp") == 0) {
        const int c = bigfrac_cmp(&x, &y);
        printf("R %d\n", (c > 0) ? 1 : ((c < 0) ? -1 : 0));
    } else if (strcmp(op, "div") == 0) {
        const bigfrac_err_ty rc = bigfrac_div(&z, &x, &y);
        if (rc == BIGFRAC_ERR_DIV_ZERO_E) {
            printf("E divzero\n");
        } else if (rc != BIGFRAC_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_frac(&z);
            printf("\n");
        }
    } else {
        bigfrac_err_ty rc = BIGFRAC_ERR_INVALID_E;
        if (strcmp(op, "add") == 0) {
            rc = bigfrac_add(&z, &x, &y);
        } else if (strcmp(op, "sub") == 0) {
            rc = bigfrac_sub(&z, &x, &y);
        } else if (strcmp(op, "mul") == 0) {
            rc = bigfrac_mul(&z, &x, &y);
        } else {
            printf("E unknown\n");
        }
        if (rc != BIGFRAC_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_frac(&z);
            printf("\n");
        }
    }

    bigfrac_free(&x);
    bigfrac_free(&y);
    bigfrac_free(&z);
}

static void frac_unary(const char *op, const char *a_str) {
    bigfrac_ty x;
    bigfrac_init(&x);

    if (parse_frac(&x, a_str) != 0) {
        printf("E parse\n");
    } else if (strcmp(op, "id") == 0) {
        printf("R ");
        print_frac(&x);
        printf("\n");
    } else if (strcmp(op, "neg") == 0) {
        if (bigfrac_neg(&x) != BIGFRAC_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_frac(&x);
            printf("\n");
        }
    } else if (strcmp(op, "inv") == 0) {
        const bigfrac_err_ty rc = bigfrac_inv(&x);
        if (rc == BIGFRAC_ERR_DIV_ZERO_E) {
            printf("E divzero\n");
        } else if (rc != BIGFRAC_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_frac(&x);
            printf("\n");
        }
    } else {
        printf("E unknown\n");
    }

    bigfrac_free(&x);
}

static void frac_fromints(const char *n_str, const char *d_str) {
    bigint_bin_ty n;
    bigint_bin_ty d;
    bigfrac_ty z;
    bigint_bin_init(&n);
    bigint_bin_init(&d);
    bigfrac_init(&z);

    bigint_err_ty rc = bigint_bin_from_str(&n, n_str, 10, NULL);
    if (rc == BIGINT_OK_E) {
        rc = bigint_bin_from_str(&d, d_str, 10, NULL);
    }
    if (rc != BIGINT_OK_E) {
        printf("E parse\n");
    } else {
        const bigfrac_err_ty frc = bigfrac_from_ints(&z, &n, &d);
        if (frc == BIGFRAC_ERR_DIV_ZERO_E) {
            printf("E divzero\n");
        } else if (frc != BIGFRAC_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_frac(&z);
            printf("\n");
        }
    }

    bigint_bin_free(&n);
    bigint_bin_free(&d);
    bigfrac_free(&z);
}

static void frac_fromstr(const char *s) {
    bigfrac_ty z;
    bigfrac_init(&z);

    const bigfrac_err_ty rc = (s != NULL)
            ? bigfrac_from_str(&z, s, NULL) : BIGFRAC_ERR_INVALID_E;
    if (rc == BIGFRAC_ERR_PARSE_E) {
        printf("E parse\n");
    } else if (rc == BIGFRAC_ERR_DIV_ZERO_E) {
        printf("E divzero\n");
    } else if (rc != BIGFRAC_OK_E) {
        printf("E err\n");
    } else {
        printf("R ");
        print_frac(&z);
        printf("\n");
    }

    bigfrac_free(&z);
}

int main(void) {
    while (fgets(g_buf, sizeof(g_buf), stdin) != NULL) {
        /* 去掉行尾换行 */
        size_t len = strlen(g_buf);
        while ((len > 0U)
                && ((g_buf[len - 1U] == '\n') || (g_buf[len - 1U] == '\r'))) {
            g_buf[--len] = '\0';
        }
        if (len == 0U) {
            continue;
        }

        char op[16];
        char *a_str = NULL;
        char *b_str = NULL;
        char *tok = strtok(g_buf, " ");
        if (tok == NULL) {
            printf("E empty\n");
            continue;
        }
        snprintf(op, sizeof(op), "%s", tok);
        tok = strtok(NULL, " ");
        if (tok != NULL) {
            a_str = tok;
        }
        tok = strtok(NULL, " ");
        if (tok != NULL) {
            b_str = tok;
        }

        if ((strcmp(op, "f_id") == 0) || (strcmp(op, "f_neg") == 0)
                || (strcmp(op, "f_inv") == 0)) {
            frac_unary(op + 2, a_str);
        } else if (strcmp(op, "f_fromstr") == 0) {
            frac_fromstr(a_str);
        } else if (strcmp(op, "f_fromints") == 0) {
            frac_fromints(a_str, b_str);
        } else if ((strcmp(op, "f_add") == 0) || (strcmp(op, "f_sub") == 0)
                || (strcmp(op, "f_mul") == 0) || (strcmp(op, "f_div") == 0)
                || (strcmp(op, "f_cmp") == 0)) {
            frac_binary(op + 2, a_str, b_str);
        } else {
            printf("E unknown\n");
        }
    }
    return 0;
}
