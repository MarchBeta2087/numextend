/* 黄金对拍驱动：行式协议，stdin 读命令，stdout 输出结果。
 *
 * 协议（空格分隔，十进制）：
 *   add a b            → R a+b
 *   sub a b            → R a-b
 *   mul a b            → R a*b
 *   mul_ntt a b        → R a*b（强制多模数 CRT NTT，设计文档 §4.3）
 *   mul_fft a b        → R a*b（强制浮点复数 FFT，设计文档 §4.3）
 *   div a b            → R q r        （截断除法）
 *   pow a e            → R a^e
 *   pow_mod b e m      → R b^e mod m
 *   shl a n            → R a<<n
 *   shr a n            → R a>>n（floor）
 *   and a b / or a b / xor a b → R 结果
 *   cmp a b            → R (-1|0|1)
 *   neg a              → R -a
 * 以上无前缀命令操作 bigint_bin；加 "d_" 前缀的命令操作 bigint_dec：
 *   d_add / d_sub / d_mul / d_div / d_cmp / d_neg / d_pow / d_pow_mod
 *   语义与 bin 版一一对应；
 *   d_mul_pow10 a n    → R a×10^n
 *   d_div_pow10 a n    → R trunc(a / 10^n)
 *   d_id a             → R a（十进制字符串解析-输出往返）
 * 交叉验证命令（经 nex_bigint_conv 互转）：
 *   c_b2d a            → R str(bin→dec(a))（a 按 bin 解析）
 *   c_d2b a            → R str(dec→bin(a))（a 按 dec 解析）
 * 输入以 EOF 结束；错误命令输出 E <msg>。
 */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
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

static void print_dec(const bigint_dec_ty *v) {
    size_t needed = 0;
    if (bigint_dec_to_str(v, 10, NULL, 0, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        return;
    }
    char *out = (char *)malloc(needed);
    if (out == NULL) {
        printf("E oom\n");
        return;
    }
    if (bigint_dec_to_str(v, 10, out, needed, &needed) != BIGINT_OK_E) {
        printf("E to_str\n");
        free(out);
        return;
    }
    printf("%s", out);
    free(out);
}

/* dec 二元运算：add / sub / mul / cmp / div；op 已剥去 "d_" 前缀 */
static void dec_binary(const char *op, const char *a_str, const char *b_str) {
    if (a_str == NULL || b_str == NULL) {
        printf("E args\n");
        return;
    }
    bigint_dec_ty x, y, z, w;
    bigint_dec_init(&x);
    bigint_dec_init(&y);
    bigint_dec_init(&z);
    bigint_dec_init(&w);

    bigint_err_ty rc = bigint_dec_from_str(&x, a_str, 10, NULL);
    if (rc == BIGINT_OK_E) rc = bigint_dec_from_str(&y, b_str, 10, NULL);
    if (rc != BIGINT_OK_E) {
        printf("E parse\n");
    } else if (strcmp(op, "cmp") == 0) {
        int c = bigint_dec_cmp(&x, &y);
        printf("R %d\n", (c > 0) ? 1 : ((c < 0) ? -1 : 0));
    } else if (strcmp(op, "div") == 0) {
        rc = bigint_dec_div_rem(&z, &w, &x, &y);
        if (rc == BIGINT_ERR_DIV_ZERO_E) {
            printf("E divzero\n");
        } else if (rc != BIGINT_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_dec(&z);
            printf(" ");
            print_dec(&w);
            printf("\n");
        }
    } else {
        if (strcmp(op, "add") == 0) rc = bigint_dec_add(&z, &x, &y);
        else if (strcmp(op, "sub") == 0) rc = bigint_dec_sub(&z, &x, &y);
        else rc = bigint_dec_mul(&z, &x, &y);
        if (rc != BIGINT_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_dec(&z);
            printf("\n");
        }
    }

    bigint_dec_free(&x);
    bigint_dec_free(&y);
    bigint_dec_free(&z);
    bigint_dec_free(&w);
}

/* dec 命令分派；op 已剥去 "d_" 前缀 */
static void handle_dec(const char *op, const char *a_str, const char *b_str,
        const char *c_str) {
    if (strcmp(op, "add") == 0 || strcmp(op, "sub") == 0
            || strcmp(op, "mul") == 0 || strcmp(op, "cmp") == 0
            || strcmp(op, "div") == 0) {
        dec_binary(op, a_str, b_str);
        return;
    }
    if (a_str == NULL) {
        printf("E args\n");
        return;
    }

    bigint_dec_ty x, y, z, w;
    bigint_dec_init(&x);
    bigint_dec_init(&y);
    bigint_dec_init(&z);
    bigint_dec_init(&w);

    bigint_err_ty rc = bigint_dec_from_str(&x, a_str, 10, NULL);
    if (rc != BIGINT_OK_E) {
        printf("E parse\n");
    } else if (strcmp(op, "id") == 0) {
        printf("R ");
        print_dec(&x);
        printf("\n");
    } else if (strcmp(op, "neg") == 0) {
        rc = bigint_dec_neg(&x);
        if (rc != BIGINT_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_dec(&x);
            printf("\n");
        }
    } else if (strcmp(op, "pow") == 0 || strcmp(op, "mul_pow10") == 0
            || strcmp(op, "div_pow10") == 0) {
        if (b_str == NULL) {
            printf("E args\n");
        } else {
            char *endp = NULL;
            unsigned long long n = strtoull(b_str, &endp, 10);
            if (endp == b_str) {
                printf("E exp\n");
            } else {
                if (strcmp(op, "pow") == 0) {
                    rc = bigint_dec_pow(&z, &x, (uint64_t)n);
                } else if (strcmp(op, "mul_pow10") == 0) {
                    rc = bigint_dec_copy(&z, &x);
                    if (rc == BIGINT_OK_E) {
                        rc = bigint_dec_mul_pow10(&z, (size_t)n);
                    }
                } else {
                    rc = bigint_dec_copy(&z, &x);
                    if (rc == BIGINT_OK_E) {
                        rc = bigint_dec_div_pow10(&z, (size_t)n);
                    }
                }
                if (rc != BIGINT_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R ");
                    print_dec(&z);
                    printf("\n");
                }
            }
        }
    } else if (strcmp(op, "pow_mod") == 0) {
        if (b_str == NULL || c_str == NULL) {
            printf("E args\n");
        } else {
            rc = bigint_dec_from_str(&y, b_str, 10, NULL);
            if (rc == BIGINT_OK_E) rc = bigint_dec_from_str(&z, c_str, 10, NULL);
            if (rc == BIGINT_OK_E) rc = bigint_dec_pow_mod(&w, &x, &y, &z);
            if (rc == BIGINT_ERR_DIV_ZERO_E) {
                printf("E divzero\n");
            } else if (rc == BIGINT_ERR_INVALID_E) {
                printf("E invalid\n");
            } else if (rc != BIGINT_OK_E) {
                printf("E err\n");
            } else {
                printf("R ");
                print_dec(&w);
                printf("\n");
            }
        }
    } else {
        printf("E unknown\n");
    }

    bigint_dec_free(&x);
    bigint_dec_free(&y);
    bigint_dec_free(&z);
    bigint_dec_free(&w);
}

/* conv 互转命令：c_b2d（bin→dec 后输出）/ c_d2b（dec→bin 后输出） */
static void handle_conv(const char *op, const char *a_str) {
    if (a_str == NULL) {
        printf("E args\n");
        return;
    }
    bigint_bin_ty xb, zb;
    bigint_dec_ty xd, zd;
    bigint_bin_init(&xb);
    bigint_bin_init(&zb);
    bigint_dec_init(&xd);
    bigint_dec_init(&zd);

    bigint_err_ty rc;
    if (strcmp(op, "c_b2d") == 0) {
        rc = bigint_bin_from_str(&xb, a_str, 10, NULL);
        if (rc == BIGINT_OK_E) rc = bigint_conv_bin_to_dec(&zd, &xb);
        if (rc != BIGINT_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_dec(&zd);
            printf("\n");
        }
    } else {
        rc = bigint_dec_from_str(&xd, a_str, 10, NULL);
        if (rc == BIGINT_OK_E) rc = bigint_conv_dec_to_bin(&zb, &xd);
        if (rc != BIGINT_OK_E) {
            printf("E err\n");
        } else {
            printf("R ");
            print_bigint(&zb);
            printf("\n");
        }
    }

    bigint_bin_free(&xb);
    bigint_bin_free(&zb);
    bigint_dec_free(&xd);
    bigint_dec_free(&zd);
}

int main(void) {
    while (fgets(g_buf, sizeof(g_buf), stdin) != NULL) {
        /* 去掉行尾换行 */
        size_t len = strlen(g_buf);
        while (len > 0 && (g_buf[len - 1] == '\n' || g_buf[len - 1] == '\r')) {
            g_buf[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }

        char op[16];
        char *a_str, *b_str, *c_str;
        a_str = NULL;
        b_str = NULL;
        c_str = NULL;

        char *tok = strtok(g_buf, " ");
        if (tok == NULL) {
            printf("E empty\n");
            continue;
        }
        snprintf(op, sizeof(op), "%s", tok);
        tok = strtok(NULL, " ");
        if (tok != NULL) a_str = tok;
        tok = strtok(NULL, " ");
        if (tok != NULL) b_str = tok;
        tok = strtok(NULL, " ");
        if (tok != NULL) c_str = tok;

        /* dec 与 conv 命令独立分派，不占用 bin 对象 */
        if (strncmp(op, "d_", 2) == 0) {
            handle_dec(op + 2, a_str, b_str, c_str);
            continue;
        }
        if (strncmp(op, "c_", 2) == 0) {
            handle_conv(op, a_str);
            continue;
        }

        bigint_bin_ty x, y, z, w, t;
        bigint_bin_init(&x);
        bigint_bin_init(&y);
        bigint_bin_init(&z);
        bigint_bin_init(&w);
        bigint_bin_init(&t);

        bigint_err_ty rc = BIGINT_OK_E;
        int done = 0;

        if (strcmp(op, "add") == 0 || strcmp(op, "sub") == 0
                || strcmp(op, "mul") == 0 || strcmp(op, "mul_ntt") == 0
                || strcmp(op, "mul_fft") == 0 || strcmp(op, "gcd") == 0
                || strcmp(op, "and") == 0 || strcmp(op, "or") == 0
                || strcmp(op, "xor") == 0 || strcmp(op, "cmp") == 0) {
            if (a_str == NULL || b_str == NULL) {
                printf("E args\n");
            } else {
                rc = bigint_bin_from_str(&x, a_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_from_str(&y, b_str, 10, NULL);
                if (rc != BIGINT_OK_E) {
                    printf("E parse\n");
                } else {
                    if (strcmp(op, "add") == 0) rc = bigint_bin_add(&z, &x, &y);
                    else if (strcmp(op, "sub") == 0) rc = bigint_bin_sub(&z, &x, &y);
                    else if (strcmp(op, "mul") == 0) rc = bigint_bin_mul(&z, &x, &y);
                    else if (strcmp(op, "gcd") == 0) rc = bigint_bin_gcd(&z, &x, &y);
                    else if (strcmp(op, "mul_ntt") == 0) {
                        /* 强制多模数 CRT NTT（设计文档 §4.3，mod_count 默认） */
                        bigint_mul_method_ty m;
                        m.algo = BIGINT_MUL_MULTI_MODULI_CRT_NTT_E;
                        m.params.multi_moduli_crt_ntt.mod_count = 0;
                        rc = bigint_bin_mul_ex(&z, &x, &y, &m);
                    }
                    else if (strcmp(op, "mul_fft") == 0) {
                        /* 强制浮点复数 FFT（设计文档 §4.3，节位宽自动） */
                        bigint_mul_method_ty m;
                        m.algo = BIGINT_MUL_FLOAT_COMPLEX_FFT_E;
                        m.params.float_complex_fft.chunk_bits = 0;
                        rc = bigint_bin_mul_ex(&z, &x, &y, &m);
                    }
                    else if (strcmp(op, "and") == 0) rc = bigint_bin_bit_and(&z, &x, &y);
                    else if (strcmp(op, "or") == 0) rc = bigint_bin_bit_or(&z, &x, &y);
                    else if (strcmp(op, "xor") == 0) rc = bigint_bin_bit_xor(&z, &x, &y);
                    else if (strcmp(op, "cmp") == 0) {
                        int c = bigint_bin_cmp(&x, &y);
                        printf("R %d\n", (c > 0) ? 1 : ((c < 0) ? -1 : 0));
                        done = 1;
                    }
                    if (!done && rc == BIGINT_OK_E) {
                        printf("R ");
                        print_bigint(&z);
                        printf("\n");
                        done = 1;
                    }
                    if (!done) printf("E err\n");
                }
            }
        } else if (strcmp(op, "div") == 0) {
            if (a_str == NULL || b_str == NULL) {
                printf("E args\n");
            } else {
                rc = bigint_bin_from_str(&x, a_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_from_str(&y, b_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_div_rem(&z, &w, &x, &y);
                if (rc == BIGINT_ERR_DIV_ZERO_E) {
                    printf("E divzero\n");
                } else if (rc != BIGINT_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R ");
                    print_bigint(&z);
                    printf(" ");
                    print_bigint(&w);
                    printf("\n");
                }
            }
        } else if (strcmp(op, "pow") == 0) {
            if (a_str == NULL || b_str == NULL) {
                printf("E args\n");
            } else {
                char *endp = NULL;
                unsigned long long e = strtoull(b_str, &endp, 10);
                if (endp == b_str) {
                    printf("E exp\n");
                } else {
                    rc = bigint_bin_from_str(&x, a_str, 10, NULL);
                    if (rc == BIGINT_OK_E) rc = bigint_bin_pow(&z, &x, (uint64_t)e);
                    if (rc != BIGINT_OK_E) {
                        printf("E err\n");
                    } else {
                        printf("R ");
                        print_bigint(&z);
                        printf("\n");
                    }
                }
            }
        } else if (strcmp(op, "pow_mod") == 0) {
            if (a_str == NULL || b_str == NULL || c_str == NULL) {
                printf("E args\n");
            } else {
                rc = bigint_bin_from_str(&x, a_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_from_str(&y, b_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_from_str(&z, c_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_pow_mod(&w, &x, &y, &z);
                if (rc == BIGINT_ERR_DIV_ZERO_E) {
                    printf("E divzero\n");
                } else if (rc == BIGINT_ERR_INVALID_E) {
                    printf("E invalid\n");
                } else if (rc != BIGINT_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R ");
                    print_bigint(&w);
                    printf("\n");
                }
            }
        } else if (strcmp(op, "shl") == 0 || strcmp(op, "shr") == 0) {
            if (a_str == NULL || b_str == NULL) {
                printf("E args\n");
            } else {
                char *endp = NULL;
                unsigned long long n = strtoull(b_str, &endp, 10);
                if (endp == b_str) {
                    printf("E bits\n");
                } else {
                    rc = bigint_bin_from_str(&x, a_str, 10, NULL);
                    if (rc == BIGINT_OK_E) {
                        if (strcmp(op, "shl") == 0) {
                            rc = bigint_bin_shl(&z, &x, (size_t)n);
                        } else {
                            rc = bigint_bin_shr(&z, &x, (size_t)n);
                        }
                    }
                    if (rc != BIGINT_OK_E) {
                        printf("E err\n");
                    } else {
                        printf("R ");
                        print_bigint(&z);
                        printf("\n");
                    }
                }
            }
        } else if (strcmp(op, "neg") == 0) {
            if (a_str == NULL) {
                printf("E args\n");
            } else {
                rc = bigint_bin_from_str(&x, a_str, 10, NULL);
                if (rc == BIGINT_OK_E) rc = bigint_bin_neg(&x);
                if (rc != BIGINT_OK_E) {
                    printf("E err\n");
                } else {
                    printf("R ");
                    print_bigint(&x);
                    printf("\n");
                }
            }
        } else {
            printf("E unknown\n");
        }

        bigint_bin_free(&x);
        bigint_bin_free(&y);
        bigint_bin_free(&z);
        bigint_bin_free(&w);
        bigint_bin_free(&t);
    }
    return 0;
}