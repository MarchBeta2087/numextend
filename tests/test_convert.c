/* 单元测试：nex_convert 转换单元（设计文档 §10 转换矩阵）。
 * 覆盖：精确互转、逆向精确转换（仅整数）、有损互转（显式 ctx 单次舍入）、
 * 复数 → 实数、特殊值映射与错误路径。 */
#include "nex/convert/nex_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

static void bf_to_s(char *buf, size_t n, const bigfloat_ty *v) {
    bigfloat_to_str(v, 0, buf, n, NULL);
}

static void bd_to_s(char *buf, size_t n, const bigdecimal_ty *v) {
    bigdecimal_to_str(v, BIGDECIMAL_FMT_FIXED_E, buf, n, NULL);
}

static void expect_bf(const char *desc, const bigfloat_ty *v,
        const char *expect) {
    char buf[256];
    bf_to_s(buf, sizeof(buf), v);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static void expect_bd(const char *desc, const bigdecimal_ty *v,
        const char *expect) {
    char buf[256];
    bd_to_s(buf, sizeof(buf), v);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
}

static void expect_bi(const char *desc, const bigint_bin_ty *v,
        const char *expect) {
    size_t need = 0;
    bigint_bin_to_str(v, 10, NULL, 0, &need);
    char *buf = (char *)malloc(need);
    if (buf == NULL) { CHECK(0); return; }
    bigint_bin_to_str(v, 10, buf, need, &need);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
    free(buf);
}

static void expect_bid(const char *desc, const bigint_dec_ty *v,
        const char *expect) {
    size_t need = 0;
    bigint_dec_to_str(v, 10, NULL, 0, &need);
    char *buf = (char *)malloc(need);
    if (buf == NULL) { CHECK(0); return; }
    bigint_dec_to_str(v, 10, buf, need, &need);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
    free(buf);
}

static void expect_frac(const char *desc, const bigfrac_ty *f,
        const char *num, const char *den) {
    size_t nl = 0, dl = 0;
    bigint_bin_to_str(&f->num, 10, NULL, 0, &nl);
    bigint_bin_to_str(&f->den, 10, NULL, 0, &dl);
    char *nb = (char *)malloc(nl), *db = (char *)malloc(dl);
    if ((nb == NULL) || (db == NULL)) { CHECK(0); free(nb); free(db); return; }
    bigint_bin_to_str(&f->num, 10, nb, nl, &nl);
    bigint_bin_to_str(&f->den, 10, db, dl, &dl);
    if ((strcmp(nb, num) != 0) || (strcmp(db, den) != 0)) {
        printf("FAIL %s: got %s/%s, expect %s/%s\n", desc, nb, db, num, den);
        g_fail++;
    }
    free(nb);
    free(db);
}

static int bf_parse(bigfloat_ty *v, const char *s) {
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    return bigfloat_from_str(v, s, &ctx, NULL) == BIGFLOAT_OK_E ? 0 : -1;
}

static int bd_parse(bigdecimal_ty *v, const char *s) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();
    return bigdecimal_from_str(v, s, &ctx, NULL) == BIGDECIMAL_OK_E ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* 1. 精确互转                                                         */
/* ------------------------------------------------------------------ */

static void test_exact(void) {
    bigfrac_ty f;
    bigint_bin_ty bi;
    bigint_dec_ty bd;
    bigint_dec_ty src_d;
    bigfloat_ty fl;
    bigdecimal_ty dc;
    bigfrac_init(&f);
    bigint_bin_init(&bi);
    bigint_dec_init(&bd);
    bigint_dec_init(&src_d);
    bigfloat_init(&fl);
    bigdecimal_init(&dc);
    const bigfloat_ctx_ty fctx = bigfloat_ctx_binary64();
    const bigdecimal_ctx_ty dctx = bigdecimal_ctx_decimal64();

    /* bin → frac（den=1）；dec → frac */
    bigint_bin_from_u64(&bi, 12345);
    CHECK(nex_convert_bin_to_frac(&f, &bi) == NEX_CONVERT_OK_E);
    expect_frac("bin→frac", &f, "12345", "1");
    bigint_dec_from_u64(&src_d, 6789);
    CHECK(nex_convert_dec_to_frac(&f, &src_d) == NEX_CONVERT_OK_E);
    expect_frac("dec→frac", &f, "6789", "1");

    /* frac → bin / dec（den=1 成功） */
    CHECK(nex_convert_frac_to_bin(&bi, &f) == NEX_CONVERT_OK_E);
    expect_bi("frac→bin", &bi, "6789");
    CHECK(nex_convert_frac_to_dec(&bd, &f) == NEX_CONVERT_OK_E);
    expect_bid("frac→dec", &bd, "6789");

    /* frac → bin / dec（den≠1 → INVALID，不截断） */
    bigint_bin_ty n2, d2;
    bigint_bin_init(&n2);
    bigint_bin_init(&d2);
    bigint_bin_from_u64(&n2, 1);
    bigint_bin_from_u64(&d2, 2);
    bigfrac_from_ints(&f, &n2, &d2);  /* 1/2 */
    CHECK(nex_convert_frac_to_bin(&bi, &f) == NEX_CONVERT_ERR_INVALID_E);
    CHECK(nex_convert_frac_to_dec(&bd, &f) == NEX_CONVERT_ERR_INVALID_E);

    /* float → frac：0.5 → 1/2；-3.5 → -7/2；2 → 2/1；0 → 0/1 */
    bf_parse(&fl, "0.5");
    CHECK(nex_convert_float_to_frac(&f, &fl) == NEX_CONVERT_OK_E);
    expect_frac("0.5→frac", &f, "1", "2");
    bf_parse(&fl, "-3.5");
    CHECK(nex_convert_float_to_frac(&f, &fl) == NEX_CONVERT_OK_E);
    expect_frac("-3.5→frac", &f, "-7", "2");
    bf_parse(&fl, "2");
    CHECK(nex_convert_float_to_frac(&f, &fl) == NEX_CONVERT_OK_E);
    expect_frac("2→frac", &f, "2", "1");
    bf_parse(&fl, "0");
    CHECK(nex_convert_float_to_frac(&f, &fl) == NEX_CONVERT_OK_E);
    expect_frac("0→frac", &f, "0", "1");
    bf_parse(&fl, "inf");
    CHECK(nex_convert_float_to_frac(&f, &fl) == NEX_CONVERT_ERR_INVALID_E);

    /* decimal → frac：0.25 → 1/4；-12.34 → -617/50；100 → 100/1 */
    bd_parse(&dc, "0.25");
    CHECK(nex_convert_decimal_to_frac(&f, &dc) == NEX_CONVERT_OK_E);
    expect_frac("0.25→frac", &f, "1", "4");
    bd_parse(&dc, "-12.34");
    CHECK(nex_convert_decimal_to_frac(&f, &dc) == NEX_CONVERT_OK_E);
    expect_frac("-12.34→frac", &f, "-617", "50");
    bd_parse(&dc, "100");
    CHECK(nex_convert_decimal_to_frac(&f, &dc) == NEX_CONVERT_OK_E);
    expect_frac("100→frac", &f, "100", "1");
    bd_parse(&dc, "inf");
    CHECK(nex_convert_decimal_to_frac(&f, &dc) == NEX_CONVERT_ERR_INVALID_E);

    bigint_bin_free(&n2);
    bigint_bin_free(&d2);
    bigdecimal_free(&dc);
    bigfloat_free(&fl);
    bigint_dec_free(&src_d);
    bigint_dec_free(&bd);
    bigint_bin_free(&bi);
    bigfrac_free(&f);
    (void)fctx;
    (void)dctx;
}

/* ------------------------------------------------------------------ */
/* 2. 逆向精确转换（仅整数）                                           */
/* ------------------------------------------------------------------ */

static void test_reverse(void) {
    bigint_bin_ty bi;
    bigint_dec_ty bd;
    bigfloat_ty fl;
    bigdecimal_ty dc;
    bigint_bin_init(&bi);
    bigint_dec_init(&bd);
    bigfloat_init(&fl);
    bigdecimal_init(&dc);

    bf_parse(&fl, "1000");
    CHECK(nex_convert_float_to_bin(&bi, &fl) == NEX_CONVERT_OK_E);
    expect_bi("1000.0→bin", &bi, "1000");
    CHECK(nex_convert_float_to_dec(&bd, &fl) == NEX_CONVERT_OK_E);
    expect_bid("1000.0→dec", &bd, "1000");
    bf_parse(&fl, "-256");
    CHECK(nex_convert_float_to_bin(&bi, &fl) == NEX_CONVERT_OK_E);
    expect_bi("-256→bin", &bi, "-256");
    bf_parse(&fl, "0");
    CHECK(nex_convert_float_to_bin(&bi, &fl) == NEX_CONVERT_OK_E);
    expect_bi("0→bin", &bi, "0");
    /* 非整数 / 特殊值 → INVALID */
    bf_parse(&fl, "0.5");
    CHECK(nex_convert_float_to_bin(&bi, &fl) == NEX_CONVERT_ERR_INVALID_E);
    bf_parse(&fl, "1e-10");
    CHECK(nex_convert_float_to_bin(&bi, &fl) == NEX_CONVERT_ERR_INVALID_E);
    bf_parse(&fl, "inf");
    CHECK(nex_convert_float_to_bin(&bi, &fl) == NEX_CONVERT_ERR_INVALID_E);
    bf_parse(&fl, "nan");
    CHECK(nex_convert_float_to_dec(&bd, &fl) == NEX_CONVERT_ERR_INVALID_E);

    bd_parse(&dc, "123456789");
    CHECK(nex_convert_decimal_to_bin(&bi, &dc) == NEX_CONVERT_OK_E);
    expect_bi("123456789→bin", &bi, "123456789");
    CHECK(nex_convert_decimal_to_dec(&bd, &dc) == NEX_CONVERT_OK_E);
    expect_bid("123456789→dec", &bd, "123456789");
    bd_parse(&dc, "-12345");
    CHECK(nex_convert_decimal_to_bin(&bi, &dc) == NEX_CONVERT_OK_E);
    expect_bi("-12345→bin", &bi, "-12345");
    bd_parse(&dc, "0");
    CHECK(nex_convert_decimal_to_bin(&bi, &dc) == NEX_CONVERT_OK_E);
    expect_bi("0→bin", &bi, "0");
    bd_parse(&dc, "0.5");
    CHECK(nex_convert_decimal_to_bin(&bi, &dc) == NEX_CONVERT_ERR_INVALID_E);
    bd_parse(&dc, "nan");
    CHECK(nex_convert_decimal_to_dec(&bd, &dc) == NEX_CONVERT_ERR_INVALID_E);

    /* NULL */
    CHECK(nex_convert_float_to_bin(NULL, &fl) == NEX_CONVERT_ERR_INVALID_E);
    CHECK(nex_convert_float_to_bin(&bi, NULL) == NEX_CONVERT_ERR_INVALID_E);

    bigdecimal_free(&dc);
    bigfloat_free(&fl);
    bigint_dec_free(&bd);
    bigint_bin_free(&bi);
}

/* ------------------------------------------------------------------ */
/* 3. 有损互转                                                         */
/* ------------------------------------------------------------------ */

static void test_rounded(void) {
    bigfrac_ty f;
    bigint_bin_ty n, d;
    bigint_dec_ty dn;
    bigfloat_ty fl;
    bigdecimal_ty dc;
    bigfrac_init(&f);
    bigint_bin_init(&n);
    bigint_bin_init(&d);
    bigint_dec_init(&dn);
    bigfloat_init(&fl);
    bigdecimal_init(&dc);
    const bigfloat_ctx_ty fctx = bigfloat_ctx_binary64();
    const bigdecimal_ctx_ty dctx = bigdecimal_ctx_decimal64();

    /* frac → float：1/3 → 0.333…；1/10 → 0.1；2/1 → 2 */
    bigint_bin_from_u64(&n, 1);
    bigint_bin_from_u64(&d, 3);
    bigfrac_from_ints(&f, &n, &d);
    CHECK(nex_convert_frac_to_float(&fl, &f, &fctx) == NEX_CONVERT_OK_E);
    double out = 0.0;
    bigfloat_to_f64(&fl, &out);
    CHECK(out == 1.0 / 3.0);
    bigint_bin_from_u64(&n, 1);
    bigint_bin_from_u64(&d, 10);
    bigfrac_from_ints(&f, &n, &d);
    CHECK(nex_convert_frac_to_float(&fl, &f, &fctx) == NEX_CONVERT_OK_E);
    bigfloat_to_f64(&fl, &out);
    CHECK(out == 0.1);
    bigint_bin_from_u64(&n, 2);
    bigint_bin_from_u64(&d, 1);
    bigfrac_from_ints(&f, &n, &d);
    CHECK(nex_convert_frac_to_float(&fl, &f, &fctx) == NEX_CONVERT_OK_E);
    expect_bf("2/1→float", &fl, "2");

    /* frac → decimal：1/3 → 0.3333333333333333；1/8 → 0.125 */
    bigint_bin_from_u64(&n, 1);
    bigint_bin_from_u64(&d, 3);
    bigfrac_from_ints(&f, &n, &d);
    CHECK(nex_convert_frac_to_decimal(&dc, &f, &dctx) == NEX_CONVERT_OK_E);
    expect_bd("1/3→decimal", &dc, "0.3333333333333333");
    bigint_bin_from_u64(&n, 1);
    bigint_bin_from_u64(&d, 8);
    bigfrac_from_ints(&f, &n, &d);
    CHECK(nex_convert_frac_to_decimal(&dc, &f, &dctx) == NEX_CONVERT_OK_E);
    expect_bd("1/8→decimal", &dc, "0.125");

    /* bigint_dec → float：10^18 → 1e18 */
    bigint_dec_from_u64(&dn, 1);
    bigint_dec_mul_pow10(&dn, 18);
    CHECK(nex_convert_dec_to_float(&fl, &dn, &fctx) == NEX_CONVERT_OK_E);
    bigfloat_to_f64(&fl, &out);
    CHECK(out == 1e18);

    /* float → decimal：binary64 0.1 → 0.1（单次舍入）；1/3 double → 0.333… */
    bf_parse(&fl, "0.1");
    CHECK(nex_convert_float_to_decimal(&dc, &fl, &dctx) == NEX_CONVERT_OK_E);
    expect_bd("float 0.1→decimal", &dc, "0.1");
    bf_parse(&fl, "0.3333333333333333");
    CHECK(nex_convert_float_to_decimal(&dc, &fl, &dctx) == NEX_CONVERT_OK_E);
    expect_bd("float 1/3→decimal", &dc, "0.3333333333333333");

    /* decimal → float：0.1 → binary64 0.1；往返 */
    bd_parse(&dc, "0.1");
    CHECK(nex_convert_decimal_to_float(&fl, &dc, &fctx) == NEX_CONVERT_OK_E);
    bigfloat_to_f64(&fl, &out);
    CHECK(out == 0.1);
    bd_parse(&dc, "3.14159265358979323846");
    CHECK(nex_convert_decimal_to_float(&fl, &dc, &fctx) == NEX_CONVERT_OK_E);
    bigfloat_to_f64(&fl, &out);
    CHECK(out == 3.141592653589793);  /* 四舍五入到 binary64 */

    /* 往返：float → decimal → float 恒等（值域重叠内） */
    const char *vals[] = { "1.5", "-12.34", "1e100", "0.0001", "3.5e-17" };
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        bf_parse(&fl, vals[i]);
        CHECK(nex_convert_float_to_decimal(&dc, &fl, &dctx)
                == NEX_CONVERT_OK_E);
        CHECK(nex_convert_decimal_to_float(&fl, &dc, &fctx)
                == NEX_CONVERT_OK_E);
        double a = 0.0, b = 0.0;
        bigfloat_to_f64(&fl, &a);
        bf_parse(&fl, vals[i]);
        bigfloat_to_f64(&fl, &b);
        CHECK(a == b);
    }

    /* 特殊值映射 */
    bf_parse(&fl, "inf");
    CHECK(nex_convert_float_to_decimal(&dc, &fl, &dctx) == NEX_CONVERT_OK_E);
    CHECK(bigdecimal_is_inf(&dc));
    bf_parse(&fl, "-inf");
    CHECK(nex_convert_float_to_decimal(&dc, &fl, &dctx) == NEX_CONVERT_OK_E);
    CHECK(dc.flag == BIGDECIMAL_NEG_INF_E);
    bf_parse(&fl, "nan");
    CHECK(nex_convert_float_to_decimal(&dc, &fl, &dctx) == NEX_CONVERT_OK_E);
    CHECK(bigdecimal_is_nan(&dc));
    bd_parse(&dc, "inf");
    CHECK(nex_convert_decimal_to_float(&fl, &dc, &fctx) == NEX_CONVERT_OK_E);
    CHECK(bigfloat_is_inf(&fl));

    /* 上溢：decimal64 范围内的 1e500 → bigfloat binary64 超范围 → ±inf */
    bd_parse(&dc, "1e500");
    CHECK(bigdecimal_is_normal(&dc));
    CHECK(nex_convert_decimal_to_float(&fl, &dc, &fctx) == NEX_CONVERT_OK_E);
    CHECK(bigfloat_is_inf(&fl));

    /* NULL / 非法 ctx */
    CHECK(nex_convert_frac_to_float(NULL, &f, &fctx)
            == NEX_CONVERT_ERR_INVALID_E);
    CHECK(nex_convert_frac_to_float(&fl, &f, NULL)
            == NEX_CONVERT_ERR_INVALID_E);

    bigdecimal_free(&dc);
    bigfloat_free(&fl);
    bigint_dec_free(&dn);
    bigint_bin_free(&d);
    bigint_bin_free(&n);
    bigfrac_free(&f);
}

/* ------------------------------------------------------------------ */
/* 4. 复数 → 实数                                                      */
/* ------------------------------------------------------------------ */

static void test_cpx(void) {
    bigcomplex_float_ty cf;
    bigcomplex_decimal_ty cd;
    bigfloat_ty fl;
    bigdecimal_ty dc;
    bigcomplex_float_init(&cf);
    bigcomplex_decimal_init(&cd);
    bigfloat_init(&fl);
    bigdecimal_init(&dc);

    CHECK(bigcomplex_float_from_str(&cf, "1+0i",
            &(bigfloat_ctx_ty){ 53, 11, 0 }, NULL) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(nex_convert_cpx_float_to_float(&fl, &cf) == NEX_CONVERT_OK_E);
    expect_bf("(1+0i)→float", &fl, "1");
    CHECK(bigcomplex_float_from_str(&cf, "1+2i",
            &(bigfloat_ctx_ty){ 53, 11, 0 }, NULL) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(nex_convert_cpx_float_to_float(&fl, &cf)
            == NEX_CONVERT_ERR_INVALID_E);
    CHECK(bigcomplex_float_from_str(&cf, "0+0i",
            &(bigfloat_ctx_ty){ 53, 11, 0 }, NULL) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(nex_convert_cpx_float_to_float(&fl, &cf) == NEX_CONVERT_OK_E);
    CHECK(bigfloat_is_zero(&fl));

    CHECK(bigcomplex_decimal_from_str(&cd, "-3.5+0i",
            &(bigdecimal_ctx_ty){ 16, 3, 0 }, NULL) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(nex_convert_cpx_decimal_to_decimal(&dc, &cd) == NEX_CONVERT_OK_E);
    expect_bd("(-3.5+0i)→decimal", &dc, "-3.5");
    CHECK(bigcomplex_decimal_from_str(&cd, "1+1i",
            &(bigdecimal_ctx_ty){ 16, 3, 0 }, NULL) == BIGCOMPLEX_DECIMAL_OK_E);
    CHECK(nex_convert_cpx_decimal_to_decimal(&dc, &cd)
            == NEX_CONVERT_ERR_INVALID_E);

    CHECK(nex_convert_cpx_float_to_float(NULL, &cf)
            == NEX_CONVERT_ERR_INVALID_E);
    CHECK(nex_convert_cpx_float_to_float(&fl, NULL)
            == NEX_CONVERT_ERR_INVALID_E);

    bigdecimal_free(&dc);
    bigfloat_free(&fl);
    bigcomplex_decimal_free(&cd);
    bigcomplex_float_free(&cf);
}

int main(void) {
    test_exact();
    test_reverse();
    test_rounded();
    test_cpx();

    if (g_fail == 0) {
        printf("test_convert: ALL PASS\n");
        return 0;
    }
    printf("test_convert: %d FAILURES\n", g_fail);
    return 1;
}
