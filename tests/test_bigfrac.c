/* 单元测试：bigfrac 全 API */
#include "nex/bigfrac/nex_bigfrac.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#endif
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

static void expect_str(const char *desc, const bigint_bin_ty *v,
        const char *expect) {
    size_t needed = 0;
    bigint_bin_to_str(v, 10, NULL, 0, &needed);
    char *buf = (char *)malloc(needed);
    if (buf == NULL) { CHECK(0); return; }
    bigint_bin_to_str(v, 10, buf, needed, &needed);
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %s: got %s, expect %s\n", desc, buf, expect);
        g_fail++;
    }
    free(buf);
}

static void expect_frac(const char *desc, const bigfrac_ty *f,
        const char *num_expect, const char *den_expect) {
    size_t nlen = 0;
    size_t dlen = 0;
    bigint_bin_to_str(&f->num, 10, NULL, 0, &nlen);
    bigint_bin_to_str(&f->den, 10, NULL, 0, &dlen);
    char *nbuf = (char *)malloc(nlen);
    char *dbuf = (char *)malloc(dlen);
    if ((nbuf == NULL) || (dbuf == NULL)) {
        CHECK(0);
        free(nbuf);
        free(dbuf);
        return;
    }
    bigint_bin_to_str(&f->num, 10, nbuf, nlen, &nlen);
    bigint_bin_to_str(&f->den, 10, dbuf, dlen, &dlen);
    if ((strcmp(nbuf, num_expect) != 0) || (strcmp(dbuf, den_expect) != 0)) {
        printf("FAIL %s: got %s/%s, expect %s/%s\n", desc, nbuf, dbuf,
                num_expect, den_expect);
        g_fail++;
    }
    free(nbuf);
    free(dbuf);
}

/* 校验规范化不变式（§3.4）：den > 0、gcd(|num|, den) == 1、零为 0/1 */
static void check_invariants(const char *desc, const bigfrac_ty *f) {
    CHECK(bigint_bin_sign(&f->den) == BIGINT_SIGN_POS_E);
    bigint_bin_ty g;
    bigint_bin_init(&g);
    CHECK(bigint_bin_gcd(&g, &f->num, &f->den) == BIGINT_OK_E);
    uint64_t gval = 0;
    CHECK(bigint_bin_to_u64(&g, &gval) == BIGINT_OK_E);
    if (gval != 1U) {
        printf("FAIL %s: gcd != 1\n", desc);
        g_fail++;
    }
    if (bigint_bin_is_zero(&f->num)) {
        expect_str("zero den", &f->den, "1");
    }
    bigint_bin_free(&g);
}

/* 以 "p/q" 或十进制字符串构造既约分数（测试辅助，非被测路径） */
static void mk_frac(bigfrac_ty *f, const char *s) {
    CHECK(bigfrac_init(f) == BIGFRAC_OK_E);
    CHECK(bigfrac_from_str(f, s, NULL) == BIGFRAC_OK_E);
}

static void test_lifecycle(void) {
    bigfrac_ty a;
    CHECK(bigfrac_init(&a) == BIGFRAC_OK_E);
    expect_frac("init", &a, "0", "1");
    CHECK(bigint_bin_sign(&a.den) == BIGINT_SIGN_POS_E);
    CHECK(bigfrac_num(&a) == &a.num);
    CHECK(bigfrac_den(&a) == &a.den);
    CHECK(bigfrac_num(NULL) == NULL);
    CHECK(bigfrac_den(NULL) == NULL);
    CHECK(bigfrac_init(NULL) == BIGFRAC_ERR_INVALID_E);

    bigfrac_ty b;
    mk_frac(&b, "-3/7");
    CHECK(bigfrac_copy(&a, &b) == BIGFRAC_OK_E);
    expect_frac("copy", &a, "-3", "7");
    CHECK(bigfrac_cmp(&a, &b) == 0);
    CHECK(bigfrac_copy(&a, &a) == BIGFRAC_OK_E);
    CHECK(bigfrac_copy(NULL, &a) == BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&a);
    bigfrac_free(&b);
    bigfrac_free(&a);  // 双重 free 必须安全
}

static void test_from_ints(void) {
    bigfrac_ty f;
    bigfrac_init(&f);

    /* 约分：2/4 → 1/2 */
    bigint_bin_ty n;
    bigint_bin_ty d;
    bigint_bin_init(&n);
    bigint_bin_init(&d);
    CHECK(bigint_bin_from_str(&n, "2", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_from_str(&d, "4", 10, NULL) == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_OK_E);
    expect_frac("2/4", &f, "1", "2");
    check_invariants("2/4", &f);

    /* 负分母：5/-3 → -5/3 */
    CHECK(bigint_bin_from_str(&n, "5", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_from_str(&d, "-3", 10, NULL) == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_OK_E);
    expect_frac("5/-3", &f, "-5", "3");
    check_invariants("5/-3", &f);

    /* 负分子负分母：-5/-3 → 5/3 */
    CHECK(bigint_bin_from_str(&n, "-5", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_from_str(&d, "-3", 10, NULL) == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_OK_E);
    expect_frac("-5/-3", &f, "5", "3");

    /* 大数约分：2^64 / (2·2^64) → 1/2 */
    CHECK(bigint_bin_from_str(&n, "18446744073709551616", 10, NULL)
            == BIGINT_OK_E);
    CHECK(bigint_bin_from_str(&d, "36893488147419103232", 10, NULL)
            == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_OK_E);
    expect_frac("2^64/(2*2^64)", &f, "1", "2");

    /* 零分子：0/5 → 0/1；0/-5 → 0/1 */
    CHECK(bigint_bin_from_str(&n, "0", 10, NULL) == BIGINT_OK_E);
    CHECK(bigint_bin_from_str(&d, "5", 10, NULL) == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_OK_E);
    expect_frac("0/5", &f, "0", "1");
    CHECK(bigint_bin_from_str(&d, "-5", 10, NULL) == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_OK_E);
    expect_frac("0/-5", &f, "0", "1");

    /* 分母为零 → DIV_ZERO，f 不变 */
    CHECK(bigint_bin_from_str(&d, "0", 10, NULL) == BIGINT_OK_E);
    CHECK(bigfrac_from_ints(&f, &n, &d) == BIGFRAC_ERR_DIV_ZERO_E);
    expect_frac("den=0 unchanged", &f, "0", "1");

    CHECK(bigfrac_from_ints(NULL, &n, &d) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_from_ints(&f, NULL, &d) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_from_ints(&f, &n, NULL) == BIGFRAC_ERR_INVALID_E);

    bigint_bin_free(&n);
    bigint_bin_free(&d);
    bigfrac_free(&f);
}

static void test_from_str(void) {
    bigfrac_ty f;
    bigfrac_init(&f);
    const char *end = NULL;

    /* 整数 */
    CHECK(bigfrac_from_str(&f, "12", &end) == BIGFRAC_OK_E);
    expect_frac("12", &f, "12", "1");
    CHECK((end != NULL) && (*end == '\0'));

    /* 分数 */
    CHECK(bigfrac_from_str(&f, "-3/7", &end) == BIGFRAC_OK_E);
    expect_frac("-3/7", &f, "-3", "7");

    /* 十进制小数精确化简：-12.340 → -617/50 */
    CHECK(bigfrac_from_str(&f, "-12.340", &end) == BIGFRAC_OK_E);
    expect_frac("-12.340", &f, "-617", "50");
    check_invariants("-12.340", &f);

    /* ".5" 与 "5." */
    CHECK(bigfrac_from_str(&f, ".5", &end) == BIGFRAC_OK_E);
    expect_frac(".5", &f, "1", "2");
    CHECK(bigfrac_from_str(&f, "5.", &end) == BIGFRAC_OK_E);
    expect_frac("5.", &f, "5", "1");

    /* 前导零小数：0.25 → 1/4 */
    CHECK(bigfrac_from_str(&f, "0.25", &end) == BIGFRAC_OK_E);
    expect_frac("0.25", &f, "1", "4");

    /* 负零与零 */
    CHECK(bigfrac_from_str(&f, "-0.0", &end) == BIGFRAC_OK_E);
    expect_frac("-0.0", &f, "0", "1");
    CHECK(bigfrac_from_str(&f, "0", &end) == BIGFRAC_OK_E);
    expect_frac("0", &f, "0", "1");

    /* 部分消费容错 */
    CHECK(bigfrac_from_str(&f, "12abc", &end) == BIGFRAC_OK_E);
    expect_frac("12abc", &f, "12", "1");
    CHECK((end != NULL) && (*end == 'a'));
    CHECK(bigfrac_from_str(&f, "1/2/3", &end) == BIGFRAC_OK_E);
    expect_frac("1/2/3", &f, "1", "2");
    CHECK((end != NULL) && (*end == '/'));
    CHECK(bigfrac_from_str(&f, "12/", &end) == BIGFRAC_OK_E);
    expect_frac("12/", &f, "12", "1");
    CHECK((end != NULL) && (*end == '/'));
    CHECK(bigfrac_from_str(&f, "12.34abc", &end) == BIGFRAC_OK_E);
    expect_frac("12.34abc", &f, "617", "50");
    CHECK((end != NULL) && (*end == 'a'));

    /* 解析失败：首字符即非法，frac 保持调用前状态（617/50） */
    CHECK(bigfrac_from_str(&f, "", &end) == BIGFRAC_ERR_PARSE_E);
    CHECK(bigfrac_from_str(&f, "-", &end) == BIGFRAC_ERR_PARSE_E);
    CHECK(bigfrac_from_str(&f, "/5", &end) == BIGFRAC_ERR_PARSE_E);
    CHECK(bigfrac_from_str(&f, ".", &end) == BIGFRAC_ERR_PARSE_E);
    expect_frac("parse unchanged", &f, "617", "50");
    CHECK((end != NULL) && (*end == '.'));

    /* "p/0" → DIV_ZERO，frac 保持调用前状态 */
    CHECK(bigfrac_from_str(&f, "1/0", &end) == BIGFRAC_ERR_DIV_ZERO_E);
    expect_frac("1/0 unchanged", &f, "617", "50");

    CHECK(bigfrac_from_str(NULL, "12", NULL) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_from_str(&f, NULL, NULL) == BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&f);
}

static void test_arith(void) {
    bigfrac_ty x;
    bigfrac_ty y;
    bigfrac_ty z;
    bigfrac_init(&z);

    /* 加：1/2 + 1/3 = 5/6；1/2 + 1/4 = 3/4（lcm 路径）；1/6 + 1/4 = 5/12 */
    mk_frac(&x, "1/2");
    mk_frac(&y, "1/3");
    CHECK(bigfrac_add(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("1/2+1/3", &z, "5", "6");
    check_invariants("1/2+1/3", &z);
    mk_frac(&y, "1/4");
    CHECK(bigfrac_add(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("1/2+1/4", &z, "3", "4");
    mk_frac(&x, "1/6");
    CHECK(bigfrac_add(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("1/6+1/4", &z, "5", "12");

    /* 加：负 + 正 = 负 */
    mk_frac(&x, "-1/2");
    mk_frac(&y, "1/4");
    CHECK(bigfrac_add(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("-1/2+1/4", &z, "-1", "4");

    /* 减：1/2 - 1/3 = 1/6；2/3 - 1/3 = 1/3；x - x = 0 */
    mk_frac(&x, "1/2");
    mk_frac(&y, "1/3");
    CHECK(bigfrac_sub(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("1/2-1/3", &z, "1", "6");
    mk_frac(&x, "2/3");
    mk_frac(&y, "1/3");
    CHECK(bigfrac_sub(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("2/3-1/3", &z, "1", "3");
    CHECK(bigfrac_sub(&z, &x, &x) == BIGFRAC_OK_E);
    expect_frac("x-x", &z, "0", "1");

    /* 乘：交叉约分 1/2 × 2/3 = 1/3；0 × 5/7 = 0 */
    mk_frac(&x, "1/2");
    mk_frac(&y, "2/3");
    CHECK(bigfrac_mul(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("1/2*2/3", &z, "1", "3");
    check_invariants("1/2*2/3", &z);
    mk_frac(&x, "0");
    mk_frac(&y, "5/7");
    CHECK(bigfrac_mul(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("0*5/7", &z, "0", "1");

    /* 除：3/2 ÷ 2/5 = 15/4；1/2 ÷ 4/3 = 3/8；除零 */
    mk_frac(&x, "3/2");
    mk_frac(&y, "2/5");
    CHECK(bigfrac_div(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("3/2/2/5", &z, "15", "4");
    check_invariants("3/2/2/5", &z);
    mk_frac(&x, "1/2");
    mk_frac(&y, "4/3");
    CHECK(bigfrac_div(&z, &x, &y) == BIGFRAC_OK_E);
    expect_frac("1/2/4/3", &z, "3", "8");
    mk_frac(&y, "0");
    CHECK(bigfrac_div(&z, &x, &y) == BIGFRAC_ERR_DIV_ZERO_E);
    expect_frac("div0 unchanged", &z, "3", "8");

    /* neg / inv */
    mk_frac(&x, "2/3");
    CHECK(bigfrac_neg(&x) == BIGFRAC_OK_E);
    expect_frac("neg 2/3", &x, "-2", "3");
    CHECK(bigfrac_neg(&x) == BIGFRAC_OK_E);
    expect_frac("neg -2/3", &x, "2", "3");
    CHECK(bigfrac_inv(&x) == BIGFRAC_OK_E);
    expect_frac("inv 2/3", &x, "3", "2");
    mk_frac(&x, "-2/3");
    CHECK(bigfrac_inv(&x) == BIGFRAC_OK_E);
    expect_frac("inv -2/3", &x, "-3", "2");
    check_invariants("inv -2/3", &x);
    mk_frac(&x, "0");
    CHECK(bigfrac_inv(&x) == BIGFRAC_ERR_DIV_ZERO_E);
    CHECK(bigfrac_neg(&x) == BIGFRAC_OK_E);
    expect_frac("neg 0", &x, "0", "1");

    CHECK(bigfrac_add(&z, NULL, &y) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_sub(&z, &x, NULL) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_mul(NULL, &x, &y) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_div(&z, NULL, NULL) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_neg(NULL) == BIGFRAC_ERR_INVALID_E);
    CHECK(bigfrac_inv(NULL) == BIGFRAC_ERR_INVALID_E);

    bigfrac_free(&x);
    bigfrac_free(&y);
    bigfrac_free(&z);
}

static void test_alias(void) {
    bigfrac_ty x;
    bigfrac_ty y;
    bigfrac_ty z;
    bigfrac_init(&z);

    /* dst 与 lhs / rhs 别名 */
    mk_frac(&x, "1/2");
    mk_frac(&y, "1/3");
    CHECK(bigfrac_copy(&z, &x) == BIGFRAC_OK_E);
    CHECK(bigfrac_add(&z, &z, &y) == BIGFRAC_OK_E);  // z = x + y
    expect_frac("alias dst==lhs", &z, "5", "6");
    CHECK(bigfrac_copy(&z, &x) == BIGFRAC_OK_E);
    CHECK(bigfrac_add(&z, &y, &z) == BIGFRAC_OK_E);  // z = y + x
    expect_frac("alias dst==rhs", &z, "5", "6");

    /* 自运算：x = x + x、x - x、x × x、x ÷ x */
    mk_frac(&x, "1/2");
    CHECK(bigfrac_add(&x, &x, &x) == BIGFRAC_OK_E);
    expect_frac("self add", &x, "1", "1");
    CHECK(bigfrac_sub(&x, &x, &x) == BIGFRAC_OK_E);
    expect_frac("self sub", &x, "0", "1");
    mk_frac(&x, "2/3");
    CHECK(bigfrac_mul(&x, &x, &x) == BIGFRAC_OK_E);
    expect_frac("self mul", &x, "4", "9");
    CHECK(bigfrac_div(&x, &x, &x) == BIGFRAC_OK_E);
    expect_frac("self div", &x, "1", "1");

    bigfrac_free(&x);
    bigfrac_free(&y);
    bigfrac_free(&z);
}

static void test_cmp(void) {
    bigfrac_ty a;
    bigfrac_ty b;
    bigfrac_init(&a);
    bigfrac_init(&b);

    /* 同号 */
    mk_frac(&a, "1/2");
    mk_frac(&b, "1/3");
    CHECK(bigfrac_cmp(&a, &b) > 0);
    CHECK(bigfrac_cmp(&b, &a) < 0);
    CHECK(bigfrac_cmp(&a, &a) == 0);

    /* 异号（符号捷径） */
    mk_frac(&a, "-1/2");
    mk_frac(&b, "1/3");
    CHECK(bigfrac_cmp(&a, &b) < 0);
    CHECK(bigfrac_cmp(&b, &a) > 0);

    /* 零 */
    mk_frac(&a, "0");
    mk_frac(&b, "1/3");
    CHECK(bigfrac_cmp(&a, &b) < 0);
    CHECK(bigfrac_cmp(&b, &a) > 0);
    mk_frac(&b, "-1/3");
    CHECK(bigfrac_cmp(&a, &b) > 0);
    mk_frac(&b, "0");
    CHECK(bigfrac_cmp(&a, &b) == 0);

    /* 值相等但表示不同（经运算产生，如 2/6 不出现，用等价分数验证） */
    mk_frac(&a, "2/6");
    mk_frac(&b, "1/3");
    CHECK(bigfrac_cmp(&a, &b) == 0);

    /* 负分数比较：-3/2 < -1/2 */
    mk_frac(&a, "-3/2");
    mk_frac(&b, "-1/2");
    CHECK(bigfrac_cmp(&a, &b) < 0);

    /* 大数比较 */
    mk_frac(&a, "1000000000000000000000000000000/3");
    mk_frac(&b, "999999999999999999999999999999/3");
    CHECK(bigfrac_cmp(&a, &b) > 0);

    bigfrac_free(&a);
    bigfrac_free(&b);
}

int main(void) {
#ifdef _MSC_VER
    /* CRT 调试堆：逐次分配完整性检查 + 退出时泄漏报告 */
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_CRT_DF
            | _CRTDBG_LEAK_CHECK_DF);
#endif

    test_lifecycle();
    test_from_ints();
    test_from_str();
    test_arith();
    test_alias();
    test_cmp();

    if (g_fail == 0) {
        printf("ALL OK\n");
        return 0;
    }
    printf("%d FAILURES\n", g_fail);
    return 1;
}
