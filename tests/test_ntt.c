/* 单元测试：nex_ntt NTT 核心模块（设计文档 §4.3） */
#include "nex/ntt/nex_ntt.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

/* 确定性 LCG 随机数（避免依赖平台库差异） */
static uint32_t g_rng = 0x12345678U;
static uint32_t next_u32(void)
{
    g_rng = g_rng * 1664525U + 1013904223U;
    return g_rng;
}

/* 取 [0, p) 均匀随机值（乘高位法） */
static uint32_t rand_less(uint32_t p)
{
    return (uint32_t)((uint64_t)next_u32() * p >> 32U);
}

/* 朴素循环卷积（模 p），作 NTT 卷积对拍基准 */
static void naive_cyclic_conv(uint32_t *out, const uint32_t *a,
        const uint32_t *b, size_t n, uint32_t p)
{
    for (size_t i = 0U; i < n; i++) {
        out[i] = 0U;
    }
    for (size_t i = 0U; i < n; i++) {
        for (size_t j = 0U; j < n; j++) {
            const size_t k = (i + j) % n;
            out[k] = (uint32_t)(((uint64_t)out[k]
                    + (uint64_t)a[i] * b[j]) % p);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 模数表与校验                                                        */
/* ------------------------------------------------------------------ */

static void test_mod_table(void)
{
    const size_t count = ntt_mod_count();
    CHECK(count > 0U);
    CHECK(ntt_mod_get(count) == NULL);
    for (size_t i = 0U; i < count; i++) {
        const ntt_mod_ty *mod = ntt_mod_get(i);
        CHECK(mod != NULL);
        CHECK(ntt_mod_validate(mod) == NTT_OK_E);
    }
}

static void test_mod_validate_rejects(void)
{
    CHECK(ntt_mod_validate(NULL) == NTT_ERR_INVALID_E);
    /* 合数：998244355 = 5·199648871 */
    const ntt_mod_ty composite = { 998244355U, 499122177U, 1U, 1U };
    CHECK(ntt_mod_validate(&composite) == NTT_ERR_MODULUS_E);
    /* k 非奇 */
    const ntt_mod_ty even_k = { 998244353U, 238U, 22U, 15311432U };
    CHECK(ntt_mod_validate(&even_k) == NTT_ERR_MODULUS_E);
    /* p−1 ≠ k·2^c */
    const ntt_mod_ty bad_kc = { 998244353U, 120U, 23U, 15311432U };
    CHECK(ntt_mod_validate(&bad_kc) == NTT_ERR_MODULUS_E);
    /* 根阶不符：w = 1 */
    const ntt_mod_ty bad_w = { 998244353U, 119U, 23U, 1U };
    CHECK(ntt_mod_validate(&bad_w) == NTT_ERR_MODULUS_E);
    /* w 越界（w == p） */
    const ntt_mod_ty w_oob = { 998244353U, 119U, 23U, 998244353U };
    CHECK(ntt_mod_validate(&w_oob) == NTT_ERR_MODULUS_E);
    /* c 越界（c = 40） */
    const ntt_mod_ty c_big = { 998244353U, 119U, 40U, 15311432U };
    CHECK(ntt_mod_validate(&c_big) == NTT_ERR_MODULUS_E);
}

/* ------------------------------------------------------------------ */
/* 模运算                                                              */
/* ------------------------------------------------------------------ */

static void test_mod_arith(void)
{
    const ntt_mod_ty *mod = ntt_mod_get(2);  /* 998244353 */
    CHECK(mod != NULL);
    CHECK(ntt_mod_mul(0U, 42U, mod) == 0U);
    CHECK(ntt_mod_mul(1U, 42U, mod) == 42U);
    /* 根定义自洽：w = g^k = 3^119 mod p */
    CHECK(ntt_mod_pow(3U, 119U, mod) == mod->w);
    /* 大基（≥ p）亦正确：p·p ≡ 0 */
    CHECK(ntt_mod_mul(mod->p, mod->p, mod) == 0U);
    for (int trial = 0; trial < 200; trial++) {
        const uint32_t a = rand_less(mod->p);
        const uint32_t b = rand_less(mod->p);
        const uint32_t c = rand_less(mod->p);
        CHECK(ntt_mod_mul(a, b, mod) == ntt_mod_mul(b, a, mod));
        CHECK(ntt_mod_mul(ntt_mod_mul(a, b, mod), c, mod)
                == ntt_mod_mul(a, ntt_mod_mul(b, c, mod), mod));
        /* Fermat：a^(p−1) ≡ 1 */
        CHECK(ntt_mod_pow(a, mod->p - 1U, mod) == 1U);
        /* 逆元：a·a^(p−2) ≡ 1（a ≠ 0） */
        if (a != 0U) {
            CHECK(ntt_mod_mul(a, ntt_mod_inv(a, mod), mod) == 1U);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 正逆变换往返                                                         */
/* ------------------------------------------------------------------ */

static void test_roundtrip_one(const ntt_mod_ty *mod, uint32_t log2n)
{
    const uint32_t n = 1U << log2n;
    uint32_t *buf = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *orig = (uint32_t *)malloc(n * sizeof(uint32_t));
    CHECK((buf != NULL) && (orig != NULL));
    if ((buf == NULL) || (orig == NULL)) {
        free(buf);
        free(orig);
        return;
    }
    for (uint32_t i = 0U; i < n; i++) {
        orig[i] = rand_less(mod->p);
        buf[i] = orig[i];
    }
    CHECK(ntt_forward(buf, log2n, mod) == NTT_OK_E);
    CHECK(ntt_inverse(buf, log2n, mod) == NTT_OK_E);
    for (uint32_t i = 0U; i < n; i++) {
        if (buf[i] != orig[i]) {
            printf("FAIL roundtrip p=%u log2n=%u idx=%u: got %u want %u\n",
                    mod->p, log2n, i, buf[i], orig[i]);
            g_fail++;
            break;
        }
    }
    free(buf);
    free(orig);
}

static void test_roundtrip(void)
{
    const size_t count = ntt_mod_count();
    for (size_t m = 0U; m < count; m++) {
        const ntt_mod_ty *mod = ntt_mod_get(m);
        test_roundtrip_one(mod, 0U);
        test_roundtrip_one(mod, 1U);
        test_roundtrip_one(mod, 6U);
        test_roundtrip_one(mod, 12U);
    }
    /* 较大长度与 p > 2^31 的模数（回绕路径）抽查 */
    test_roundtrip_one(ntt_mod_get(0), 16U);
    test_roundtrip_one(ntt_mod_get(10), 16U);  /* 2281701377 > 2^31 */
    test_roundtrip_one(ntt_mod_get(0), 18U);
}

/* ------------------------------------------------------------------ */
/* 卷积对拍                                                             */
/* ------------------------------------------------------------------ */

static void test_convolution_one(const ntt_mod_ty *mod, uint32_t log2n)
{
    const uint32_t n = 1U << log2n;
    uint32_t *a = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *b = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *got = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *want = (uint32_t *)malloc(n * sizeof(uint32_t));
    CHECK((a != NULL) && (b != NULL) && (got != NULL) && (want != NULL));
    if ((a == NULL) || (b == NULL) || (got == NULL) || (want == NULL)) {
        free(a);
        free(b);
        free(got);
        free(want);
        return;
    }
    for (uint32_t i = 0U; i < n; i++) {
        a[i] = rand_less(mod->p);
        b[i] = rand_less(mod->p);
        got[i] = a[i];
    }
    /* 对拍基准须在变换前计算：ntt_forward 就地改写 b */
    naive_cyclic_conv(want, a, b, n, mod->p);
    CHECK(ntt_forward(got, log2n, mod) == NTT_OK_E);
    CHECK(ntt_forward(b, log2n, mod) == NTT_OK_E);
    CHECK(ntt_pointwise_mul(got, got, b, n, mod) == NTT_OK_E);
    CHECK(ntt_inverse(got, log2n, mod) == NTT_OK_E);
    for (uint32_t i = 0U; i < n; i++) {
        if (got[i] != want[i]) {
            printf("FAIL conv p=%u log2n=%u idx=%u: got %u want %u\n",
                    mod->p, log2n, i, got[i], want[i]);
            g_fail++;
            break;
        }
    }
    free(a);
    free(b);
    free(got);
    free(want);
}

static void test_convolution(void)
{
    const size_t count = ntt_mod_count();
    for (size_t m = 0U; m < count; m++) {
        const ntt_mod_ty *mod = ntt_mod_get(m);
        test_convolution_one(mod, 4U);
        test_convolution_one(mod, 8U);
    }
    test_convolution_one(ntt_mod_get(2), 10U);
}

/* ------------------------------------------------------------------ */
/* CRT 重构                                                            */
/* ------------------------------------------------------------------ */

static void test_crt(void)
{
    /* 两模数：内置表前两个（Πp ≈ 2^60.6 < 2^64） */
    const ntt_mod_ty *mods2[2];
    mods2[0] = ntt_mod_get(0);
    mods2[1] = ntt_mod_get(1);
    CHECK(mods2[0] != NULL);
    CHECK(mods2[1] != NULL);
    const uint64_t prod2 = (uint64_t)mods2[0]->p * mods2[1]->p;
    CHECK(prod2 < (UINT64_C(1) << 63U));
    for (int trial = 0; trial < 200; trial++) {
        const uint64_t v = (uint64_t)next_u32() * next_u32() % prod2;
        const uint32_t residues[2] = {
            (uint32_t)(v % mods2[0]->p),
            (uint32_t)(v % mods2[1]->p)
        };
        CHECK(ntt_crt_reconstruct_one(residues, mods2, 2U) == v);
    }

    /* 三模数：自备小模数 {17, 41, 97}（Πp = 67609 < 2^64），验证递推 */
    const ntt_mod_ty m17 = { 17U, 1U, 4U, 3U };
    const ntt_mod_ty m41 = { 41U, 5U, 3U, 38U };
    const ntt_mod_ty m97 = { 97U, 3U, 5U, 28U };
    CHECK(ntt_mod_validate(&m17) == NTT_OK_E);
    CHECK(ntt_mod_validate(&m41) == NTT_OK_E);
    CHECK(ntt_mod_validate(&m97) == NTT_OK_E);
    const ntt_mod_ty *mods3[3] = { &m17, &m41, &m97 };
    const uint64_t prod3 = (uint64_t)17U * 41U * 97U;
    for (int trial = 0; trial < 200; trial++) {
        const uint64_t v = (uint64_t)next_u32() % prod3;
        const uint32_t residues[3] = {
            (uint32_t)(v % 17U),
            (uint32_t)(v % 41U),
            (uint32_t)(v % 97U)
        };
        CHECK(ntt_crt_reconstruct_one(residues, mods3, 3U) == v);
    }

    /* 端到端：三模数 NTT 卷积 + CRT 重构 == 精确循环卷积
       （输入 < 17，n = 8（受限于最小 c = 3）→ 系数 ≤ 8·16² = 2048
       < Πp = 67609） */
    {
        const uint32_t n = 8U;
        uint32_t a[8];
        uint32_t b[8];
        uint32_t ra[3][8];
        uint32_t rb[3][8];
        uint64_t want[8] = { 0 };
        for (uint32_t i = 0U; i < n; i++) {
            a[i] = (uint32_t)next_u32() % 17U;
            b[i] = (uint32_t)next_u32() % 17U;
        }
        for (uint32_t i = 0U; i < n; i++) {
            for (uint32_t j = 0U; j < n; j++) {
                want[(i + j) % n] += (uint64_t)a[i] * b[j];
            }
        }
        for (size_t m = 0U; m < 3U; m++) {
            const ntt_mod_ty *mod = mods3[m];
            for (uint32_t i = 0U; i < n; i++) {
                ra[m][i] = a[i];
                rb[m][i] = b[i];
            }
            CHECK(ntt_forward(ra[m], 3U, mod) == NTT_OK_E);
            CHECK(ntt_forward(rb[m], 3U, mod) == NTT_OK_E);
            CHECK(ntt_pointwise_mul(ra[m], ra[m], rb[m], n, mod)
                    == NTT_OK_E);
            CHECK(ntt_inverse(ra[m], 3U, mod) == NTT_OK_E);
        }
        for (uint32_t i = 0U; i < n; i++) {
            const uint32_t residues[3] = { ra[0][i], ra[1][i], ra[2][i] };
            CHECK(ntt_crt_reconstruct_one(residues, mods3, 3U) == want[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 非法参数                                                             */
/* ------------------------------------------------------------------ */

static void test_invalid_args(void)
{
    const ntt_mod_ty *mod = ntt_mod_get(0);  /* c = 21 */
    uint32_t buf[8] = { 0 };
    CHECK(ntt_forward(NULL, 3U, mod) == NTT_ERR_INVALID_E);
    CHECK(ntt_forward(buf, 3U, NULL) == NTT_ERR_INVALID_E);
    CHECK(ntt_inverse(NULL, 3U, mod) == NTT_ERR_INVALID_E);
    CHECK(ntt_inverse(buf, 3U, NULL) == NTT_ERR_INVALID_E);
    /* log2n 超出模数能力（22 > 21） */
    CHECK(ntt_forward(buf, 22U, mod) == NTT_ERR_INVALID_E);
    CHECK(ntt_inverse(buf, 22U, mod) == NTT_ERR_INVALID_E);
    /* log2n 过大（未校验模数也会触发防御护栏） */
    CHECK(ntt_forward(buf, 40U, mod) == NTT_ERR_INVALID_E);
    /* 点乘参数检查 */
    CHECK(ntt_pointwise_mul(NULL, buf, buf, 8U, mod) == NTT_ERR_INVALID_E);
    CHECK(ntt_pointwise_mul(buf, NULL, buf, 8U, mod) == NTT_ERR_INVALID_E);
    CHECK(ntt_pointwise_mul(buf, buf, NULL, 8U, mod) == NTT_ERR_INVALID_E);
    CHECK(ntt_pointwise_mul(buf, buf, buf, 8U, NULL) == NTT_ERR_INVALID_E);
    /* 合法调用确认 */
    CHECK(ntt_forward(buf, 3U, mod) == NTT_OK_E);
    CHECK(ntt_pointwise_mul(buf, buf, buf, 8U, mod) == NTT_OK_E);
    CHECK(ntt_inverse(buf, 3U, mod) == NTT_OK_E);
}

int main(void)
{
    test_mod_table();
    test_mod_validate_rejects();
    test_mod_arith();
    test_roundtrip();
    test_convolution();
    test_crt();
    test_invalid_args();
    if (g_fail == 0) {
        printf("ntt: all tests passed\n");
    }
    return (g_fail == 0) ? 0 : 1;
}
