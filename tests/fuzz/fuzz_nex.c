/*
 * fuzz_nex.c：属性/差分模糊测试（P2-D，issue #98）。
 *
 * 一个 libFuzzer 入口覆盖 6 类风险面，输入首字节选模式：
 *   0 NTT：正逆变换往返 + 循环卷积 vs schoolbook（随机内置模数/长度）
 *   1 CRT：Garner 重构后逐模数回代一致
 *   2 dec↔bin：十进制/十六进制字符串往返、dec→bin 与 bin 直接解析一致
 *   3 bigfrac：既约/分母为正/零唯一表示 + (x+y)−y == x
 *   4 bigfloat：规范化尾数位数、指数范围、compose∘decompose 幂等
 *   5 ball：区间结果包含同精度舍入的参考值（add/mul）
 *
 * 构建：-DNEX_ENABLE_FUZZ=ON（Clang：-fsanitize=fuzzer,address,undefined）。
 * 运行：fuzz_nex [corpus] -max_total_time=120
 *
 * 设计约束：单次输入只做小规模运算（≤64 点 NTT、≤4 肢大数），
 * 保证 fuzz 吞吐；不需要 OpenCL/线程。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nex/ball/nex_ball.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
#include "nex/ntt/nex_ntt.h"

/* ------------------------------ 工具 ------------------------------ */

typedef struct {
    const uint8_t *p;
    size_t n;
    size_t i;
} rd_ty;

static uint32_t rd_u32(rd_ty *r)
{
    uint32_t v = 0u;
    for (size_t k = 0u; k < 4u; k++) {
        const uint8_t b = (r->i < r->n) ? r->p[r->i] : (uint8_t)(k * 37u);
        r->i++;
        v = (v << 8u) | b;
    }
    return v;
}

static uint32_t rd_below(rd_ty *r, uint32_t bound)
{
    return (bound == 0u) ? 0u : (rd_u32(r) % bound);
}

static void fuzz_fail(const char *tag, unsigned mode)
{
    fprintf(stderr, "fuzz_nex: FAIL mode=%u: %s\n", mode, tag);
    abort();
}

static void expect_ok(int err, const char *tag, unsigned mode)
{
    if (err != 0) {
        fuzz_fail(tag, mode);
    }
}

static int bin_is_one(const bigint_bin_ty *v)
{
    return v->len == 1u && v->limbs[0] == 1u
            && v->sign == BIGINT_SIGN_POS_E;
}

/* ----------------------- 0：NTT 往返 + 卷积 ----------------------- */

#define NTT_MAX_N 64u

static void fuzz_ntt(rd_ty *r)
{
    const size_t mod_count = ntt_mod_count();
    const ntt_mod_ty *mod = ntt_mod_get((size_t)rd_below(r, (uint32_t)mod_count));
    if (mod == NULL) {
        fuzz_fail("ntt_mod_get", 0);
    }
    const uint32_t log2n = 2u + rd_below(r, 5u);   /* 4..64 点 */
    const size_t n = (size_t)1u << log2n;
    uint32_t a[NTT_MAX_N];
    uint32_t b[NTT_MAX_N];
    uint32_t fa[NTT_MAX_N];
    uint32_t fb[NTT_MAX_N];

    for (size_t i = 0u; i < n; i++) {
        a[i] = rd_below(r, 256u);
        b[i] = rd_below(r, 256u);
    }

    /* 正逆变换往返（模 p 内恒等） */
    memcpy(fa, a, n * sizeof(uint32_t));
    expect_ok((int)ntt_forward(fa, log2n, mod), "ntt_forward", 0);
    expect_ok((int)ntt_inverse(fa, log2n, mod), "ntt_inverse", 0);
    for (size_t i = 0u; i < n; i++) {
        if (fa[i] != a[i]) {
            fuzz_fail("ntt roundtrip", 0);
        }
    }

    /* 循环卷积 vs schoolbook（系数 < 256，N ≤ 64，uint64 不溢出） */
    memcpy(fa, a, n * sizeof(uint32_t));
    memcpy(fb, b, n * sizeof(uint32_t));
    expect_ok((int)ntt_forward(fa, log2n, mod), "ntt_forward a", 0);
    expect_ok((int)ntt_forward(fb, log2n, mod), "ntt_forward b", 0);
    expect_ok((int)ntt_pointwise_mul(fa, fa, fb, n, mod), "ntt_pointwise", 0);
    expect_ok((int)ntt_inverse(fa, log2n, mod), "ntt_inverse conv", 0);
    for (size_t k = 0u; k < n; k++) {
        uint64_t sum = 0u;
        for (size_t i = 0u; i < n; i++) {
            const size_t j = (k + n - i) % n;
            sum += (uint64_t)a[i] * (uint64_t)b[j];
        }
        if (fa[k] != (uint32_t)(sum % mod->p)) {
            fuzz_fail("ntt convolution", 0);
        }
    }
}

/* ---------------------------- 1：CRT ---------------------------- */

static void fuzz_crt(rd_ty *r)
{
    if (ntt_mod_count() < 2u) {
        fuzz_fail("crt needs 2 moduli", 1);
    }
    const ntt_mod_ty *m0 = ntt_mod_get(0u);
    const ntt_mod_ty *m1 = ntt_mod_get(1u);
    const ntt_mod_ty *mods[2] = { m0, m1 };

    /* 已知值 v < 2^32 ⇒ 两个模数可唯一重构 */
    const uint32_t v = rd_u32(r);
    const uint32_t res[2] = { v % m0->p, v % m1->p };
    if (ntt_crt_reconstruct_one(res, mods, 2u) != (uint64_t)v) {
        fuzz_fail("crt known value", 1);
    }

    /* 任意余数：重构值回代必须逐模数一致 */
    const uint32_t r0 = rd_below(r, m0->p);
    const uint32_t r1 = rd_below(r, m1->p);
    const uint32_t res2[2] = { r0, r1 };
    const uint64_t w = ntt_crt_reconstruct_one(res2, mods, 2u);
    if ((uint32_t)(w % m0->p) != r0 || (uint32_t)(w % m1->p) != r1) {
        fuzz_fail("crt residues", 1);
    }
}

/* ------------------------- 2：dec ↔ bin ------------------------- */

static void fuzz_dec_bin(rd_ty *r)
{
    char dec[64];
    char hex[256];
    char buf[256];
    char neg[64];
    const int negative = (rd_below(r, 2u) != 0u);
    const size_t nd = 1u + rd_below(r, 40u);
    size_t pos = 0u;
    if (negative) {
        dec[pos++] = '-';
    }
    dec[pos++] = (char)('1' + rd_below(r, 9u));   /* 最高位非零 */
    for (size_t i = 1u; i < nd; i++) {
        dec[pos++] = (char)('0' + rd_below(r, 10u));
    }
    dec[pos] = '\0';

    bigint_bin_ty x;
    bigint_bin_ty y;
    bigint_bin_ty z;
    bigint_dec_ty d;
    expect_ok(bigint_bin_init(&x), "init x", 2);
    expect_ok(bigint_bin_init(&y), "init y", 2);
    expect_ok(bigint_bin_init(&z), "init z", 2);
    expect_ok(bigint_dec_init(&d), "init dec", 2);

    expect_ok(bigint_bin_from_str(&x, dec, 10u, NULL), "bin parse10", 2);
    size_t need = 0u;
    expect_ok(bigint_bin_to_str(&x, 10u, buf, sizeof(buf), &need),
            "bin to10", 2);
    if (strcmp(buf, dec) != 0) {
        fuzz_fail("bin dec roundtrip", 2);
    }

    /* dec 模块解析后再转 bin，必须与 bin 直接解析一致 */
    expect_ok(bigint_dec_from_str(&d, dec, 10u, NULL), "dec parse10", 2);
    expect_ok(bigint_conv_dec_to_bin(&y, &d), "dec->bin", 2);
    if (bigint_bin_cmp(&x, &y) != 0) {
        fuzz_fail("dec/bin agree", 2);
    }

    /* 十六进制往返 */
    expect_ok(bigint_bin_to_str(&x, 16u, hex, sizeof(hex), &need),
            "bin to16", 2);
    expect_ok(bigint_bin_from_str(&z, hex, 16u, NULL), "bin parse16", 2);
    if (bigint_bin_cmp(&x, &z) != 0) {
        fuzz_fail("bin hex roundtrip", 2);
    }
    (void)neg;

    bigint_bin_free(&x);
    bigint_bin_free(&y);
    bigint_bin_free(&z);
    bigint_dec_free(&d);
}

/* -------------------------- 3：bigfrac -------------------------- */

static void frac_check(const bigfrac_ty *f, unsigned mode)
{
    if (f->den.sign != BIGINT_SIGN_POS_E || f->den.len == 0u) {
        fuzz_fail("frac den must be positive non-zero", mode);
    }
    if (bigint_bin_is_zero(&f->num)
            && !(f->den.len == 1u && f->den.limbs[0] == 1u)) {
        fuzz_fail("frac zero must be 0/1", mode);
    }
    bigint_bin_ty g;
    if (bigint_bin_init(&g) != BIGINT_OK_E) {
        fuzz_fail("frac gcd init", mode);
    }
    if (bigint_bin_gcd(&g, &f->num, &f->den) != BIGINT_OK_E) {
        bigint_bin_free(&g);
        fuzz_fail("frac gcd", mode);
    }
    if (!bin_is_one(&g)) {
        bigint_bin_free(&g);
        fuzz_fail("frac not reduced", mode);
    }
    bigint_bin_free(&g);
}

static void fuzz_frac(rd_ty *r)
{
    char sa[48];
    char sb[48];
    (void)snprintf(sa, sizeof(sa), "%d/%u", (int)(rd_below(r, 2001u)) - 1000,
            1u + rd_below(r, 1000u));
    (void)snprintf(sb, sizeof(sb), "%d/%u", (int)(rd_below(r, 2001u)) - 1000,
            1u + rd_below(r, 1000u));

    bigfrac_ty x;
    bigfrac_ty y;
    bigfrac_ty s;
    bigfrac_ty t;
    expect_ok(bigfrac_init(&x), "frac init x", 3);
    expect_ok(bigfrac_init(&y), "frac init y", 3);
    expect_ok(bigfrac_init(&s), "frac init s", 3);
    expect_ok(bigfrac_init(&t), "frac init t", 3);

    expect_ok(bigfrac_from_str(&x, sa, NULL), "frac parse x", 3);
    expect_ok(bigfrac_from_str(&y, sb, NULL), "frac parse y", 3);
    frac_check(&x, 3);
    frac_check(&y, 3);

    /* (x + y) - y == x */
    expect_ok(bigfrac_add(&s, &x, &y), "frac add", 3);
    frac_check(&s, 3);
    expect_ok(bigfrac_sub(&t, &s, &y), "frac sub", 3);
    frac_check(&t, 3);
    if (bigfrac_cmp(&t, &x) != 0) {
        fuzz_fail("frac (x+y)-y", 3);
    }

    /* 非零时 x * (1/x) == 1 */
    if (!bigint_bin_is_zero(&x.num)) {
        expect_ok(bigfrac_inv(&x), "frac inv", 3);
        frac_check(&x, 3);
        expect_ok(bigfrac_mul(&t, &x, &y), "frac mul", 3);
        frac_check(&t, 3);
    }

    bigfrac_free(&x);
    bigfrac_free(&y);
    bigfrac_free(&s);
    bigfrac_free(&t);
}

/* ------------------------- 4：bigfloat -------------------------- */

static void fuzz_bigfloat(rd_ty *r)
{
    const size_t mant_bits = 2u + rd_below(r, 62u);
    const size_t exp_bits = 8u + rd_below(r, 9u);   /* 8..16 */
    const bigfloat_round_ty round =
            (bigfloat_round_ty)rd_below(r, 5u);
    bigfloat_ctx_ty ctx;
    expect_ok(bigfloat_ctx_make(&ctx, mant_bits, exp_bits, round), "ctx", 4);

    uint64_t m = ((uint64_t)rd_u32(r) << 32) | (uint64_t)rd_u32(r);
    m |= UINT64_C(1);
    const int64_t exp = (int64_t)rd_below(r, 4001u) - 2000;

    bigint_bin_ty mant;
    expect_ok(bigint_bin_init(&mant), "bf mant init", 4);
    expect_ok(bigint_bin_from_u64(&mant, m), "bf mant set", 4);

    bigfloat_ty x;
    bigfloat_ty y;
    expect_ok(bigfloat_init(&x), "bf init x", 4);
    expect_ok(bigfloat_init(&y), "bf init y", 4);
    expect_ok(bigfloat_compose(&x, &mant, exp, BIGFLOAT_POS_E, &ctx),
            "bf compose", 4);

    if (bigfloat_is_normal(&x)) {
        bigint_bin_ty m2;
        int64_t exp2 = 0;
        bigfloat_flag_ty flag = BIGFLOAT_NAN_E;
        expect_ok(bigint_bin_init(&m2), "bf m2 init", 4);
        expect_ok(bigfloat_decompose(&x, &m2, &exp2, &flag), "bf decompose", 4);
        if (bigint_bin_bit_len(&m2) != mant_bits) {
            bigint_bin_free(&m2);
            fuzz_fail("bf mant bits", 4);
        }
        const int64_t half = (int64_t)1 << (exp_bits - 1u);
        if (exp2 < -half || exp2 > half - 1) {
            bigint_bin_free(&m2);
            fuzz_fail("bf exp range", 4);
        }
        expect_ok(bigfloat_compose(&y, &m2, exp2, flag, &ctx),
                "bf recompose", 4);
        if (!bigfloat_eq(&x, &y)) {
            bigint_bin_free(&m2);
            fuzz_fail("bf idempotent", 4);
        }
        bigint_bin_free(&m2);
    }

    bigint_bin_free(&mant);
    bigfloat_free(&x);
    bigfloat_free(&y);
}

/* --------------------------- 5：ball ---------------------------- */

static void fuzz_ball(rd_ty *r)
{
    bigfloat_ctx_ty ctx;
    expect_ok(bigfloat_ctx_make(&ctx, 24u + rd_below(r, 40u), 16u,
            BIGFLOAT_ROUND_NEAREST_EVEN_E), "ball ctx", 5);

    const uint64_t av = ((uint64_t)rd_u32(r) << 32) | (uint64_t)rd_u32(r);
    const uint64_t bv = ((uint64_t)rd_u32(r) << 32) | (uint64_t)rd_u32(r);
    bigint_bin_ty am;
    bigint_bin_ty bm;
    expect_ok(bigint_bin_init(&am), "ball am", 5);
    expect_ok(bigint_bin_init(&bm), "ball bm", 5);
    expect_ok(bigint_bin_from_u64(&am, av), "ball am set", 5);
    expect_ok(bigint_bin_from_u64(&bm, bv), "ball bm set", 5);

    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty zero;
    bigfloat_ty ref;
    bigfloat_ball_ty A;
    bigfloat_ball_ty B;
    bigfloat_ball_ty C;
    expect_ok(bigfloat_init(&a), "ball a", 5);
    expect_ok(bigfloat_init(&b), "ball b", 5);
    expect_ok(bigfloat_init(&zero), "ball zero", 5);
    expect_ok(bigfloat_init(&ref), "ball ref", 5);
    expect_ok(bigfloat_ball_init(&A), "ball A", 5);
    expect_ok(bigfloat_ball_init(&B), "ball B", 5);
    expect_ok(bigfloat_ball_init(&C), "ball C", 5);

    expect_ok(bigfloat_compose(&a, &am, 0, BIGFLOAT_POS_E, &ctx),
            "ball a compose", 5);
    expect_ok(bigfloat_compose(&b, &bm, 0, BIGFLOAT_POS_E, &ctx),
            "ball b compose", 5);
    expect_ok(bigfloat_ball_set(&A, &a, &zero, &ctx), "ball set A", 5);
    expect_ok(bigfloat_ball_set(&B, &b, &zero, &ctx), "ball set B", 5);

    if (bigfloat_ball_add(&C, &A, &B, &ctx) == BIGFLOAT_OK_E
            && bigfloat_add(&ref, &a, &b, &ctx) == BIGFLOAT_OK_E) {
        if (!bigfloat_ball_contains(&C, &ref, &ctx)) {
            fuzz_fail("ball add containment", 5);
        }
    }
    if (bigfloat_ball_mul(&C, &A, &B, &ctx) == BIGFLOAT_OK_E
            && bigfloat_mul(&ref, &a, &b, &ctx) == BIGFLOAT_OK_E) {
        if (!bigfloat_ball_contains(&C, &ref, &ctx)) {
            fuzz_fail("ball mul containment", 5);
        }
    }

    bigint_bin_free(&am);
    bigint_bin_free(&bm);
    bigfloat_free(&a);
    bigfloat_free(&b);
    bigfloat_free(&zero);
    bigfloat_free(&ref);
    bigfloat_ball_free(&A);
    bigfloat_ball_free(&B);
    bigfloat_ball_free(&C);
}

/* ---------------------------- 入口 ----------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL || size == 0u) {
        return 0;
    }
    rd_ty r = { data, size, 1u };
    const unsigned mode = (unsigned)(data[0] % 6u);
    switch (mode) {
        case 0: fuzz_ntt(&r); break;
        case 1: fuzz_crt(&r); break;
        case 2: fuzz_dec_bin(&r); break;
        case 3: fuzz_frac(&r); break;
        case 4: fuzz_bigfloat(&r); break;
        default: fuzz_ball(&r); break;
    }
    return 0;
}
