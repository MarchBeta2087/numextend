/*
 * harness_bigint_bin.c：CBMC 有界验证（P2-C，issue #98）。
 *
 * 覆盖 nex/bigint/bin 的信任基：
 *   - bigint_bin_add / bigint_bin_sub（含 dst 与源别名）
 *   - bigint_bin_mul_ex（AUTO，小规模走 schoolbook）
 *   - bigint_bin_cmp
 *
 * 方法：输入肢数由编译期 IN_LIMBS 固定（0..3），肢值任意但规范化；用一份
 * 独立的 16-bit digit 参考模型对拍，并检查输出规范化不变式。CBMC 同时打开
 * 溢出/越界/指针/移位检查，因此对「所有」满足假设的输入成立（有界模型
 * 检查，非抽样测试）。肢数固定是为了让循环边界成为常量，避免符号长度导致
 * 的公式爆炸；0..3 全部跑一遍即为有界完备。
 *
 * 不覆盖：除法、NTT/FFT、dec 模块、浮点/ball（见 verify/cbmc/README.md）。
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_alloc.h"

#ifndef MUL_K
#define MUL_K 3u             /* harness_mul_const 的常数乘子（0..3） */
#endif
#ifndef IN_LIMBS
#define IN_LIMBS 3u          /* A 的肢数 */
#endif
#ifndef IN_LIMBS_B
#define IN_LIMBS_B IN_LIMBS  /* B 的肢数；取不同值以覆盖不等长路径 */
#endif
#if IN_LIMBS > 3u || IN_LIMBS_B > 3u
#error "单操作数最多 3 肢（96 bit）；更大规模会让 CBMC 公式爆炸"
#endif
#if IN_LIMBS > IN_LIMBS_B
#define MAX_IN_LIMBS IN_LIMBS
#else
#define MAX_IN_LIMBS IN_LIMBS_B
#endif

#define ADD_OUT_LIMBS (MAX_IN_LIMBS + 1u)
#define MUL_OUT_LIMBS (IN_LIMBS + IN_LIMBS_B)
#define REF_DIGITS (MUL_OUT_LIMBS * 2u + 2u)

typedef struct {
    int sign;                        /* -1 / 0 / +1 */
    size_t n;                        /* 有效 digit 数，零为 0 */
    uint32_t d[REF_DIGITS];          /* 16-bit digits，小端 */
} ref_ty;

/* ---------------- 参考模型（与被测代码无共享） ---------------- */

static void ref_norm(ref_ty *r)
{
    while (r->n > 0u && r->d[r->n - 1u] == 0u) {
        r->n--;
    }
    if (r->n == 0u) {
        r->sign = 0;
    }
}

static int ref_cmp_mag(const ref_ty *a, const ref_ty *b)
{
    if (a->n != b->n) {
        return (a->n < b->n) ? -1 : 1;
    }
    for (size_t i = a->n; i-- > 0u;) {
        if (a->d[i] != b->d[i]) {
            return (a->d[i] < b->d[i]) ? -1 : 1;
        }
    }
    return 0;
}

static void ref_add_mag(ref_ty *r, const ref_ty *a, const ref_ty *b)
{
    const size_t n = (a->n > b->n) ? a->n : b->n;
    uint32_t carry = 0u;
    for (size_t i = 0u; i < n; i++) {
        const uint32_t av = (i < a->n) ? a->d[i] : 0u;
        const uint32_t bv = (i < b->n) ? b->d[i] : 0u;
        const uint32_t s = av + bv + carry;
        r->d[i] = s & 0xFFFFu;
        carry = s >> 16u;
    }
    r->n = n;
    if (carry != 0u && r->n < REF_DIGITS) {
        r->d[r->n++] = carry;
    }
    ref_norm(r);
}

/* 要求 |a| >= |b| */
static void ref_sub_mag(ref_ty *r, const ref_ty *a, const ref_ty *b)
{
    int32_t borrow = 0;
    for (size_t i = 0u; i < a->n; i++) {
        const int32_t bv = (i < b->n) ? (int32_t)b->d[i] : 0;
        int32_t cur = (int32_t)a->d[i] - bv - borrow;
        if (cur < 0) {
            cur += 65536;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->d[i] = (uint32_t)cur;
    }
    r->n = a->n;
    ref_norm(r);
}

static void ref_add_signed(ref_ty *r, const ref_ty *a, const ref_ty *b)
{
    if (a->sign == 0) {
        *r = *b;
        return;
    }
    if (b->sign == 0) {
        *r = *a;
        return;
    }
    if (a->sign == b->sign) {
        ref_add_mag(r, a, b);
        r->sign = a->sign;
        return;
    }
    const int cmp = ref_cmp_mag(a, b);
    if (cmp == 0) {
        memset(r, 0, sizeof(*r));
        return;
    }
    if (cmp > 0) {
        ref_sub_mag(r, a, b);
        r->sign = a->sign;
    } else {
        ref_sub_mag(r, b, a);
        r->sign = b->sign;
    }
}

static void ref_sub_signed(ref_ty *r, const ref_ty *a, const ref_ty *b)
{
    ref_ty nb = *b;
    if (nb.sign != 0) {
        nb.sign = -nb.sign;
    }
    ref_add_signed(r, a, &nb);
}

static void ref_mul(ref_ty *r, const ref_ty *a, const ref_ty *b)
{
    memset(r, 0, sizeof(*r));
    if (a->sign == 0 || b->sign == 0) {
        return;
    }
    for (size_t i = 0u; i < a->n; i++) {
        uint32_t carry = 0u;
        for (size_t j = 0u; j < b->n; j++) {
            const uint64_t cur = (uint64_t)a->d[i] * b->d[j]
                    + r->d[i + j] + carry;
            r->d[i + j] = (uint32_t)(cur & 0xFFFFu);
            carry = (uint32_t)(cur >> 16u);
        }
        size_t k = i + b->n;
        while (carry != 0u && k < REF_DIGITS) {
            const uint32_t cur = r->d[k] + carry;
            r->d[k] = cur & 0xFFFFu;
            carry = cur >> 16u;
            k++;
        }
    }
    r->n = a->n + b->n;
    r->sign = a->sign * b->sign;
    ref_norm(r);
}

/* ---------------- 被测对象 <-> 参考模型 ---------------- */

static int sign_to_int(bigint_sign_ty s)
{
    if (s == BIGINT_SIGN_POS_E) {
        return 1;
    }
    if (s == BIGINT_SIGN_NEG_E) {
        return -1;
    }
    return 0;
}

static void ref_from_bin(ref_ty *r, const bigint_bin_ty *v)
{
    memset(r, 0, sizeof(*r));
    r->n = v->len * 2u;
    for (size_t i = 0u; i < v->len; i++) {
        r->d[2u * i] = v->limbs[i] & 0xFFFFu;
        r->d[2u * i + 1u] = (v->limbs[i] >> 16u) & 0xFFFFu;
    }
    r->sign = sign_to_int(v->sign);
    ref_norm(r);
}

#define DEFINE_MAKE_BIN(name, NL) static void name(bigint_bin_ty *v) {     if ((NL) == 0u) {         const bigint_err_ty err = bigint_bin_init_cap(v, 1u);         __CPROVER_assume(err == BIGINT_OK_E);         v->len = 0u;         v->sign = BIGINT_SIGN_ZERO_E;         return;     }     uint32_t limbs[(NL)];     bigint_sign_ty sign;     __CPROVER_assume(sign == BIGINT_SIGN_POS_E             || sign == BIGINT_SIGN_NEG_E);     __CPROVER_assume(limbs[(NL) - 1u] != 0u);   /* 规范化 */     const bigint_err_ty err = bigint_bin_init_cap(v, (NL));     __CPROVER_assume(err == BIGINT_OK_E);     for (size_t i = 0u; i < (NL); i++) {         v->limbs[i] = limbs[i];     }     v->len = (NL);     v->sign = sign; }

DEFINE_MAKE_BIN(make_bin_a, IN_LIMBS)
DEFINE_MAKE_BIN(make_bin_b, IN_LIMBS_B)

/* 输出规范化 + 与参考模型逐 digit 相等 */
static void check_result(const bigint_bin_ty *got, const ref_ty *want,
        size_t max_len)
{
    __CPROVER_assert(got->len <= max_len, "len within bound");
    if (got->len == 0u) {
        __CPROVER_assert(got->sign == BIGINT_SIGN_ZERO_E, "zero sign");
    } else {
        __CPROVER_assert(got->sign != BIGINT_SIGN_ZERO_E, "non-zero sign");
        __CPROVER_assert(got->limbs[got->len - 1u] != 0u,
                "normalized top limb");
    }
    __CPROVER_assert(sign_to_int(got->sign) == want->sign, "sign matches");
    size_t i;
    const size_t nd = got->len * 2u;
    for (i = 0u; i < REF_DIGITS; i++) {
        uint32_t gd = 0u;
        if (i < nd) {
            const uint32_t limb = got->limbs[i / 2u];
            gd = (i & 1u) ? ((limb >> 16u) & 0xFFFFu) : (limb & 0xFFFFu);
        }
        const uint32_t wd = (i < want->n) ? want->d[i] : 0u;
        __CPROVER_assert(gd == wd, "digit matches");
    }
    (void)i;
}

/* ---------------- harness ---------------- */

void harness_add(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);
    ref_add_signed(&want, &ra, &rb);

    (void)bigint_bin_init(&c);
    const bigint_err_ty err = bigint_bin_add(&c, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "add returns ok");
    check_result(&c, &want, ADD_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
}

void harness_sub(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);
    ref_sub_signed(&want, &ra, &rb);

    (void)bigint_bin_init(&c);
    const bigint_err_ty err = bigint_bin_sub(&c, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "sub returns ok");
    check_result(&c, &want, ADD_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
}

/* dst 与 lhs 别名：走 add_sub 的临时对象路径 */
void harness_add_alias(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);
    ref_add_signed(&want, &ra, &rb);

    const bigint_err_ty err = bigint_bin_add(&a, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "alias add returns ok");
    check_result(&a, &want, ADD_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
}

void harness_sub_alias(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);
    ref_sub_signed(&want, &ra, &rb);

    const bigint_err_ty err = bigint_bin_sub(&a, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "alias sub returns ok");
    check_result(&a, &want, ADD_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
}

void harness_mul(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);
    ref_mul(&want, &ra, &rb);

    (void)bigint_bin_init(&c);
    const bigint_err_ty err = bigint_bin_mul(&c, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "mul returns ok");
    check_result(&c, &want, MUL_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
}

/* 乘法只查安全性质（无 UB/规范化/符号），不做逐 digit 对拍：
 * 32×32→64 的乘法等价性对 SAT 是硬问题（单个 digit 断言约 1 分钟），
 * 全量对拍放到 IN_LIMBS=1 的手动/夜间目标，见 README。 */
void harness_mul_safe(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;

    make_bin_a(&a);
    make_bin_b(&b);
    (void)bigint_bin_init(&c);
    const bigint_err_ty err = bigint_bin_mul(&c, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "mul returns ok");
    __CPROVER_assert(c.len <= MUL_OUT_LIMBS, "mul len within bound");
    if (c.len == 0u) {
        __CPROVER_assert(c.sign == BIGINT_SIGN_ZERO_E, "mul zero sign");
    } else {
        __CPROVER_assert(c.sign != BIGINT_SIGN_ZERO_E, "mul non-zero sign");
        __CPROVER_assert(c.limbs[c.len - 1u] != 0u, "mul normalized");
    }
    if (a.len == 0u || b.len == 0u) {
        __CPROVER_assert(c.len == 0u, "mul by zero");
    } else {
        const int want_sign = (a.sign == b.sign) ? 1 : -1;
        __CPROVER_assert(sign_to_int(c.sign) == want_sign, "mul sign");
    }

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
}

/* 小常数乘子：等价性在常数操作数下退化为移位/加法，SAT 很快；
 * 覆盖进位链与累加器（0/1/2/3）。 */
void harness_mul_const(void)
{
    const uint32_t k = MUL_K;
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty c;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    ref_from_bin(&ra, &a);
    (void)bigint_bin_init_cap(&b, 1u);
    if (k != 0u) {
        b.len = 1u;
        b.sign = BIGINT_SIGN_POS_E;
        b.limbs[0] = k;
    }
    ref_from_bin(&rb, &b);
    ref_mul(&want, &ra, &rb);

    (void)bigint_bin_init(&c);
    const bigint_err_ty err = bigint_bin_mul(&c, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "mul const ok");
    check_result(&c, &want, MUL_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
    bigint_bin_free(&c);
}

void harness_mul_alias(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    ref_ty ra, rb, want;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);
    ref_mul(&want, &ra, &rb);

    const bigint_err_ty err = bigint_bin_mul(&a, &a, &b);
    __CPROVER_assert(err == BIGINT_OK_E, "alias mul returns ok");
    check_result(&a, &want, MUL_OUT_LIMBS);

    bigint_bin_free(&a);
    bigint_bin_free(&b);
}

void harness_cmp(void)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    ref_ty ra, rb;

    make_bin_a(&a);
    make_bin_b(&b);
    ref_from_bin(&ra, &a);
    ref_from_bin(&rb, &b);

    int want;
    if (ra.sign != rb.sign) {
        want = (ra.sign < rb.sign) ? -1 : 1;
    } else if (ra.sign == 0) {
        want = 0;
    } else {
        const int cmp = ref_cmp_mag(&ra, &rb);
        want = (ra.sign > 0) ? cmp : -cmp;
    }
    const int got = bigint_bin_cmp(&a, &b);
    __CPROVER_assert((got < 0) == (want < 0), "cmp sign");
    __CPROVER_assert((got == 0) == (want == 0), "cmp equality");
    __CPROVER_assert(bigint_bin_cmp(&a, &a) == 0, "cmp reflexive");

    bigint_bin_free(&a);
    bigint_bin_free(&b);
}
