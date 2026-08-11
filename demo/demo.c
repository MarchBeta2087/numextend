/*
 * demo.c：Numextend 库能力演示——七大类型全家桶。
 *
 * 内容：
 *   1. bigint_bin：Fibonacci(200)
 *   2. bigint_dec：100!（十进制肢）
 *   3. bigfrac：精确有理数运算 1/2 + 1/3 + 1/6 = 1
 *   4. bigfloat：Gauss-Legendre 计算 π（binary128，约 34 位）
 *   5. bigdecimal：Gauss-Legendre 计算 π（decimal128，34 位）
 *   6. bigcomplex：复数恒等式 (3+4i)(3−4i)=25、(1+i)/(1−i)=i
 *   7. 转换：double 0.1 ↔ decimal128；bigfloat 精确分数展开
 *
 * 构建：cmake 下自动生成 demo 目标；或
 *   cc -std=c99 -I. demo/demo.c build/libnex.a -o demo
 */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"
#include "nex/convert/nex_convert.h"
#include <stdio.h>
#include <stdlib.h>

/* 打印 bigint_bin 十进制串 */
static void print_bin(const char *label, const bigint_bin_ty *v) {
    size_t need = 0;
    bigint_bin_to_str(v, 10, NULL, 0, &need);
    char *buf = (char *)malloc(need);
    if (buf == NULL) {
        return;
    }
    bigint_bin_to_str(v, 10, buf, need, &need);
    printf("%s%s\n", label, buf);
    free(buf);
}

static void print_dec(const char *label, const bigint_dec_ty *v) {
    size_t need = 0;
    bigint_dec_to_str(v, 10, NULL, 0, &need);
    char *buf = (char *)malloc(need);
    if (buf == NULL) {
        return;
    }
    bigint_dec_to_str(v, 10, buf, need, &need);
    printf("%s%s\n", label, buf);
    free(buf);
}

/* Fibonacci（迭代，bigint_bin） */
static void demo_fibonacci(void) {
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&c);
    bigint_bin_from_u64(&a, 0U);
    bigint_bin_from_u64(&b, 1U);
    for (int i = 2; i <= 200; i++) {
        bigint_bin_add(&c, &a, &b);
        bigint_bin_copy(&a, &b);
        bigint_bin_copy(&b, &c);
    }
    print_bin("Fibonacci(200) = ", &b);
    bigint_bin_free(&c);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
}

/* 100!（bigint_dec） */
static void demo_factorial(void) {
    bigint_dec_ty acc;
    bigint_dec_ty i;
    bigint_dec_ty one;
    bigint_dec_ty lim;
    bigint_dec_init(&acc);
    bigint_dec_init(&i);
    bigint_dec_init(&one);
    bigint_dec_init(&lim);
    bigint_dec_from_u64(&acc, 1U);
    bigint_dec_from_u64(&i, 2U);
    bigint_dec_from_u64(&one, 1U);
    bigint_dec_from_u64(&lim, 100U);
    while (bigint_dec_cmp(&i, &lim) <= 0) {
        bigint_dec_mul(&acc, &acc, &i);
        bigint_dec_add(&i, &i, &one);
    }
    print_dec("100! = ", &acc);
    bigint_dec_free(&lim);
    bigint_dec_free(&one);
    bigint_dec_free(&i);
    bigint_dec_free(&acc);
}

/* 精确有理数：1/2 + 1/3 + 1/6 = 1 */
static void demo_fraction(void) {
    bigfrac_ty a;
    bigfrac_ty b;
    bigfrac_ty c;
    bigint_bin_ty n;
    bigint_bin_ty d;
    bigfrac_init(&a);
    bigfrac_init(&b);
    bigfrac_init(&c);
    bigint_bin_init(&n);
    bigint_bin_init(&d);

    bigint_bin_from_u64(&n, 1U);
    bigint_bin_from_u64(&d, 2U);
    bigfrac_from_ints(&a, &n, &d);        /* 1/2 */
    bigint_bin_from_u64(&n, 1U);
    bigint_bin_from_u64(&d, 3U);
    bigfrac_from_ints(&b, &n, &d);        /* 1/3 */
    bigfrac_add(&c, &a, &b);              /* 5/6 */
    bigint_bin_from_u64(&n, 1U);
    bigint_bin_from_u64(&d, 6U);
    bigfrac_from_ints(&b, &n, &d);
    bigfrac_add(&c, &c, &b);              /* 1/2+1/3+1/6 = 1 */

    size_t need = 0;
    bigint_bin_to_str(&c.num, 10, NULL, 0, &need);
    char *nb = (char *)malloc(need);
    bigint_bin_to_str(&c.num, 10, nb, need, &need);
    bigint_bin_to_str(&c.den, 10, NULL, 0, &need);
    char *db = (char *)malloc(need);
    bigint_bin_to_str(&c.den, 10, db, need, &need);
    printf("1/2 + 1/3 + 1/6 = %s/%s (精确有理数，无舍入)\n", nb, db);
    free(nb);
    free(db);

    bigint_bin_free(&d);
    bigint_bin_free(&n);
    bigfrac_free(&c);
    bigfrac_free(&b);
    bigfrac_free(&a);
}

/* Gauss-Legendre 迭代计算 π */
static void gauss_legendre_bf(bigfloat_ty *pi, const bigfloat_ctx_ty *ctx) {
    bigfloat_ty a, b, t, p;
    bigfloat_ty an, btmp, t2, p2, tmp, tmp2, half, two, quarter, four;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&t);
    bigfloat_init(&p);
    bigfloat_init(&an);
    bigfloat_init(&btmp);
    bigfloat_init(&t2);
    bigfloat_init(&p2);
    bigfloat_init(&tmp);
    bigfloat_init(&tmp2);
    bigfloat_init(&half);
    bigfloat_init(&two);
    bigfloat_init(&quarter);
    bigfloat_init(&four);
    bigfloat_from_str(&a, "1", ctx, NULL);
    bigfloat_from_str(&half, "0.5", ctx, NULL);
    bigfloat_from_str(&two, "2", ctx, NULL);
    bigfloat_from_str(&quarter, "0.25", ctx, NULL);
    bigfloat_from_str(&four, "4", ctx, NULL);
    bigfloat_from_str(&b, "2", ctx, NULL);
    bigfloat_sqrt(&b, &b, ctx);
    bigfloat_mul(&b, &b, &half, ctx);     /* b = 1/√2 */
    bigfloat_copy(&t, &quarter);
    bigfloat_copy(&p, &a);
    for (int i = 0; i < 6; i++) {
        bigfloat_add(&an, &a, &b, ctx);
        bigfloat_mul(&an, &an, &half, ctx);       /* an = (a+b)/2 */
        bigfloat_mul(&btmp, &a, &b, ctx);
        bigfloat_sqrt(&b, &btmp, ctx);            /* b = √(a·b) */
        bigfloat_sub(&tmp, &a, &an, ctx);
        bigfloat_mul(&tmp, &tmp, &tmp, ctx);      /* (a−an)² */
        bigfloat_mul(&tmp2, &p, &tmp, ctx);
        bigfloat_sub(&t, &t, &tmp2, ctx);         /* t = t − p(a−an)² */
        bigfloat_copy(&a, &an);
        bigfloat_mul(&p, &p, &two, ctx);          /* p = 2p */
    }
    bigfloat_add(&tmp, &a, &b, ctx);
    bigfloat_mul(&tmp, &tmp, &tmp, ctx);          /* (a+b)² */
    bigfloat_mul(&tmp2, &four, &t, ctx);          /* 4t */
    bigfloat_div(pi, &tmp, &tmp2, ctx);
    bigfloat_free(&four);
    bigfloat_free(&quarter);
    bigfloat_free(&two);
    bigfloat_free(&half);
    bigfloat_free(&tmp2);
    bigfloat_free(&tmp);
    bigfloat_free(&p2);
    bigfloat_free(&t2);
    bigfloat_free(&btmp);
    bigfloat_free(&an);
    bigfloat_free(&p);
    bigfloat_free(&t);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

static void gauss_legendre_bd(bigdecimal_ty *pi, const bigdecimal_ctx_ty *ctx) {
    bigdecimal_ty a, b, t, p;
    bigdecimal_ty an, btmp, tmp, tmp2, half, two, quarter, four;
    bigdecimal_init(&a);
    bigdecimal_init(&b);
    bigdecimal_init(&t);
    bigdecimal_init(&p);
    bigdecimal_init(&an);
    bigdecimal_init(&btmp);
    bigdecimal_init(&tmp);
    bigdecimal_init(&tmp2);
    bigdecimal_init(&half);
    bigdecimal_init(&two);
    bigdecimal_init(&quarter);
    bigdecimal_init(&four);
    bigdecimal_from_str(&a, "1", ctx, NULL);
    bigdecimal_from_str(&half, "0.5", ctx, NULL);
    bigdecimal_from_str(&two, "2", ctx, NULL);
    bigdecimal_from_str(&quarter, "0.25", ctx, NULL);
    bigdecimal_from_str(&four, "4", ctx, NULL);
    bigdecimal_from_str(&b, "2", ctx, NULL);
    bigdecimal_sqrt(&b, &b, ctx);
    bigdecimal_mul(&b, &b, &half, ctx);
    bigdecimal_copy(&t, &quarter);
    bigdecimal_copy(&p, &a);
    for (int i = 0; i < 6; i++) {
        bigdecimal_add(&an, &a, &b, ctx);
        bigdecimal_mul(&an, &an, &half, ctx);
        bigdecimal_mul(&btmp, &a, &b, ctx);
        bigdecimal_sqrt(&b, &btmp, ctx);
        bigdecimal_sub(&tmp, &a, &an, ctx);
        bigdecimal_mul(&tmp, &tmp, &tmp, ctx);
        bigdecimal_mul(&tmp2, &p, &tmp, ctx);
        bigdecimal_sub(&t, &t, &tmp2, ctx);
        bigdecimal_copy(&a, &an);
        bigdecimal_mul(&p, &p, &two, ctx);
    }
    bigdecimal_add(&tmp, &a, &b, ctx);
    bigdecimal_mul(&tmp, &tmp, &tmp, ctx);
    bigdecimal_mul(&tmp2, &four, &t, ctx);
    bigdecimal_div(pi, &tmp, &tmp2, ctx);
    bigdecimal_free(&four);
    bigdecimal_free(&quarter);
    bigdecimal_free(&two);
    bigdecimal_free(&half);
    bigdecimal_free(&tmp2);
    bigdecimal_free(&tmp);
    bigdecimal_free(&btmp);
    bigdecimal_free(&an);
    bigdecimal_free(&p);
    bigdecimal_free(&t);
    bigdecimal_free(&b);
    bigdecimal_free(&a);
}

static void demo_pi(void) {
    /* bigfloat（binary128，113 位尾数 ≈ 34 位十进制） */
    const bigfloat_ctx_ty fctx = bigfloat_ctx_binary128();
    bigfloat_ty pi;
    bigfloat_init(&pi);
    gauss_legendre_bf(&pi, &fctx);
    char buf[128];
    bigfloat_to_str(&pi, 0, buf, sizeof(buf), NULL);
    printf("pi (bigfloat binary128) = %s\n", buf);

    /* bigdecimal（decimal128，34 位有效数字） */
    const bigdecimal_ctx_ty dctx = bigdecimal_ctx_decimal128();
    bigdecimal_ty pid;
    bigdecimal_init(&pid);
    gauss_legendre_bd(&pid, &dctx);
    bigdecimal_to_str(&pid, BIGDECIMAL_FMT_FIXED_E, buf, sizeof(buf), NULL);
    printf("pi (bigdecimal decimal128) = %s\n", buf);

    bigdecimal_free(&pid);
    bigfloat_free(&pi);
}

static void demo_complex(void) {
    const bigfloat_ctx_ty fctx = bigfloat_ctx_binary64();
    bigcomplex_float_ty z1, z2, z3;
    bigfloat_ty mag;
    bigcomplex_float_init(&z1);
    bigcomplex_float_init(&z2);
    bigcomplex_float_init(&z3);
    bigfloat_init(&mag);
    bigcomplex_float_from_str(&z1, "3+4i", &fctx, NULL);
    bigcomplex_float_from_str(&z2, "3-4i", &fctx, NULL);
    bigcomplex_float_mul(&z3, &z1, &z2, &fctx);
    char buf[128];
    bigcomplex_float_to_str(&z3, 0, buf, sizeof(buf), NULL);
    printf("(3+4i)(3-4i) = %s\n", buf);
    bigcomplex_float_abs(&mag, &z1, &fctx);
    bigfloat_to_str(&mag, 0, buf, sizeof(buf), NULL);
    printf("|3+4i| = %s\n", buf);
    bigcomplex_float_from_str(&z1, "1+1i", &fctx, NULL);
    bigcomplex_float_from_str(&z2, "1-1i", &fctx, NULL);
    bigcomplex_float_div(&z3, &z1, &z2, &fctx);
    bigcomplex_float_to_str(&z3, 0, buf, sizeof(buf), NULL);
    printf("(1+i)/(1-i) = %s\n", buf);
    bigfloat_free(&mag);
    bigcomplex_float_free(&z3);
    bigcomplex_float_free(&z2);
    bigcomplex_float_free(&z1);
}

static void demo_convert(void) {
    /* double 0.1 的精确二进制值 → decimal128（经精确有理数单次舍入） */
    const bigfloat_ctx_ty fctx = bigfloat_ctx_binary64();
    const bigdecimal_ctx_ty dctx = bigdecimal_ctx_decimal128();
    bigfloat_ty x;
    bigdecimal_ty y;
    bigfrac_ty f;
    bigfloat_init(&x);
    bigdecimal_init(&y);
    bigfrac_init(&f);
    bigfloat_from_f64(&x, 0.1);
    nex_convert_float_to_decimal(&y, &x, &dctx);
    char buf[128];
    bigdecimal_to_str(&y, BIGDECIMAL_FMT_FIXED_E, buf, sizeof(buf), NULL);
    printf("double 0.1 -> decimal128 = %s（十进制语义，无二进制尾差）\n", buf);
    /* 精确分数展开：0.1 的 double 精确值 */
    nex_convert_float_to_frac(&f, &x);
    size_t nlen = 0, dlen = 0;
    bigint_bin_to_str(&f.num, 10, NULL, 0, &nlen);
    bigint_bin_to_str(&f.den, 10, NULL, 0, &dlen);
    char *nb = (char *)malloc(nlen), *db = (char *)malloc(dlen);
    bigint_bin_to_str(&f.num, 10, nb, nlen, &nlen);
    bigint_bin_to_str(&f.den, 10, db, dlen, &dlen);
    printf("double 0.1 精确分数 = %s/%s\n", nb, db);
    free(nb);
    free(db);
    /* 十进制 0.1 → bigfloat：还原为 double 0.1（往返恒等） */
    bigdecimal_from_str(&y, "0.1", &dctx, NULL);
    nex_convert_decimal_to_float(&x, &y, &fctx);
    bigfloat_to_str(&x, 0, buf, sizeof(buf), NULL);
    printf("decimal 0.1 -> binary64 = %s\n", buf);
    bigfrac_free(&f);
    bigdecimal_free(&y);
    bigfloat_free(&x);
}

int main(void) {
    printf("=== Numextend 能力演示 ===\n\n");
    demo_fibonacci();
    demo_factorial();
    demo_fraction();
    printf("\n");
    demo_pi();
    printf("\n");
    demo_complex();
    printf("\n");
    demo_convert();
    printf("\n演示结束\n");
    return 0;
}
