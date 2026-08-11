/* 舍入边界构造测试：精确位于"半 ulp"（ties）点的值，验证最近偶规则。
 *
 * 设计 §12 要求"构造距舍入点恰 1 ulp 半的用例"。随机生成几乎不可能
 * 恰好命中 tie（概率测度为零），必须程序化构造：
 *   - bigfloat：p 位尾数 m 与 (m+1) 的网格中点在 (2m+1)·2^(e−1)——
 *     compose 该值（p+1 位）按最近偶舍入，必须落在尾数为偶的一侧；
 *   - bigdecimal：中点在 (10m+5)·10^(e−1)，同理。
 *
 * 覆盖 p ∈ {24, 53, 113}（bigfloat）与 {7, 16, 34}（bigdecimal），
 * 每精度数百个随机 m（固定种子 LCG），含边界 m = 2^p−1 / 10^p−1。
 */
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
    } \
} while (0)

static uint64_t rnd64(uint64_t *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s;
}

/* 随机 p 位正整数（最高位恒 1） */
static void rnd_pbit_bin(bigint_bin_ty *v, uint64_t *s, size_t p) {
    bigint_bin_from_u64(v, 1U);
    bigint_bin_shl(v, v, p - 1U);
    for (size_t i = 0U; i < p - 1U; i++) {
        if ((rnd64(s) & 1U) != 0U) {
            bigint_bin_bit_set(v, i, true);
        }
    }
}

static void test_bigfloat_ties(void) {
    const size_t ps[] = { 24U, 53U, 113U };
    const size_t ebs[] = { 8U, 11U, 15U };
    uint64_t seed = 0x5eed1234abcd5678ULL;
    bigint_bin_ty m;
    bigint_bin_ty tie;
    bigint_bin_ty two;
    bigint_bin_ty one;
    bigint_bin_init(&m);
    bigint_bin_init(&tie);
    bigint_bin_init(&two);
    bigint_bin_init(&one);
    bigint_bin_from_u64(&two, 2U);
    bigint_bin_from_u64(&one, 1U);

    for (size_t pi = 0U; pi < sizeof(ps) / sizeof(ps[0]); pi++) {
        const size_t p = ps[pi];
        const size_t eb = ebs[pi];
        bigfloat_ctx_ty ctx;
        bigfloat_ctx_make(&ctx, p, eb, BIGFLOAT_ROUND_NEAREST_EVEN_E);
        for (int i = 0; i < 300; i++) {
            if (i == 0) {
                bigint_bin_from_u64(&m, 1U);
                bigint_bin_shl(&m, &m, p);               // 2^p
                bigint_bin_sub(&m, &m, &one);            // m = 2^p − 1（全 1，奇）
            } else {
                rnd_pbit_bin(&m, &seed, p);
            }
            const int64_t e = (int64_t)(rnd64(&seed) % 40U) - 20;
            /* tie = (2m+1) × 2^(e−1)（p+1 位尾数，恰在 m×2^e 与 (m+1)×2^e 中点） */
            bigint_bin_copy(&tie, &m);
            bigint_bin_mul(&tie, &tie, &two);
            bigint_bin_add(&tie, &tie, &one);
            const int64_t tie_exp = e - 1;

            bigfloat_ty got;
            bigfloat_ty expected;
            bigfloat_init(&got);
            bigfloat_init(&expected);
            CHECK(bigfloat_compose(&got, &tie, tie_exp, BIGFLOAT_POS_E, &ctx)
                    == BIGFLOAT_OK_E);
            /* 期望 = (m 偶 ? m : m+1) × 2^e（奇偶用 bit 0 判定，
             * 尾数可超 64 位，不能经 to_u64） */
            const bool m_odd = bigint_bin_bit_test(&m, 0U);
            bigint_bin_ty exp_mant;
            bigint_bin_init(&exp_mant);
            if (!m_odd) {
                bigint_bin_copy(&exp_mant, &m);
            } else {
                bigint_bin_add(&exp_mant, &m, &one);
            }
            CHECK(bigfloat_compose(&expected, &exp_mant, e, BIGFLOAT_POS_E,
                    &ctx) == BIGFLOAT_OK_E);
            if (!bigfloat_eq(&got, &expected)) {
                char gb[128], eb_[128];
                bigfloat_to_str(&got, 0, gb, sizeof(gb), NULL);
                bigfloat_to_str(&expected, 0, eb_, sizeof(eb_), NULL);
                printf("FAIL bf tie p=%zu m_odd=%d: got %s expect %s\n", p,
                        (int)m_odd, gb, eb_);
                g_fail++;
            }
            bigint_bin_free(&exp_mant);
            bigfloat_free(&expected);
            bigfloat_free(&got);
        }
    }
    printf("bigfloat ties: OK\n");
    bigint_bin_free(&one);
    bigint_bin_free(&two);
    bigint_bin_free(&tie);
    bigint_bin_free(&m);
}

/* 随机 p 位十进制尾数（首位非 0，个位随机） */
static void rnd_pdigit_dec(bigint_dec_ty *v, uint64_t *s, size_t p) {
    bigint_dec_from_u64(v, (uint64_t)(rnd64(s) % 9U) + 1U);
    for (size_t i = 1U; i < p; i++) {
        bigint_dec_mul_pow10(v, 1U);
        bigint_dec_ty d;
        bigint_dec_init(&d);
        bigint_dec_from_u64(&d, rnd64(s) % 10U);
        bigint_dec_add(v, v, &d);
        bigint_dec_free(&d);
    }
}

static void test_bigdecimal_ties(void) {
    const size_t ps[] = { 7U, 16U, 34U };
    const size_t ebs[] = { 2U, 3U, 4U };
    uint64_t seed = 0xdeadbeef00cafe11ULL;
    bigint_dec_ty m;
    bigint_dec_ty tie;
    bigint_dec_ty ten;
    bigint_dec_ty five;
    bigint_dec_ty one;
    bigint_dec_init(&m);
    bigint_dec_init(&tie);
    bigint_dec_init(&ten);
    bigint_dec_init(&five);
    bigint_dec_init(&one);
    bigint_dec_from_u64(&ten, 10U);
    bigint_dec_from_u64(&five, 5U);
    bigint_dec_from_u64(&one, 1U);

    for (size_t pi = 0U; pi < sizeof(ps) / sizeof(ps[0]); pi++) {
        const size_t p = ps[pi];
        const size_t eb = ebs[pi];
        bigdecimal_ctx_ty ctx;
        bigdecimal_ctx_make(&ctx, p, eb, BIGDECIMAL_ROUND_NEAREST_EVEN_E);
        for (int i = 0; i < 300; i++) {
            if (i == 0) {
                /* m = 10^p − 1（全 9，奇数）*/
                bigint_dec_from_u64(&m, 1U);
                bigint_dec_mul_pow10(&m, p);
                bigint_dec_sub(&m, &m, &one);
            } else {
                rnd_pdigit_dec(&m, &seed, p);
            }
            const int64_t e = (int64_t)(rnd64(&seed) % 40U) - 20;
            /* tie = (10m+5) × 10^(e−1)（p+1 位，恰在 m×10^e 与 (m+1)×10^e 中点） */
            bigint_dec_copy(&tie, &m);
            bigint_dec_mul_pow10(&tie, 1U);
            bigint_dec_add(&tie, &tie, &five);
            const int64_t tie_exp = e - 1;

            bigdecimal_ty got;
            bigdecimal_ty expected;
            bigdecimal_init(&got);
            bigdecimal_init(&expected);
            CHECK(bigdecimal_compose(&got, &tie, tie_exp, BIGDECIMAL_POS_E,
                    &ctx) == BIGDECIMAL_OK_E);
            /* 期望 = (m 偶 ? m : m+1) × 10^e（奇偶取个位数字，
             * 尾数可超 uint64，不能经 to_u64） */
            bigint_dec_ty mrem;
            bigint_dec_init(&mrem);
            bigint_dec_div_rem(NULL, &mrem, &m, &ten);
            uint64_t mlow = 0;
            bigint_dec_to_u64(&mrem, &mlow);
            const bool m_odd = ((mlow & 1U) != 0U);
            bigint_dec_free(&mrem);
            bigint_dec_ty exp_mant;
            bigint_dec_init(&exp_mant);
            if (!m_odd) {
                bigint_dec_copy(&exp_mant, &m);
            } else {
                bigint_dec_add(&exp_mant, &m, &one);
            }
            CHECK(bigdecimal_compose(&expected, &exp_mant, e,
                    BIGDECIMAL_POS_E, &ctx) == BIGDECIMAL_OK_E);
            if (!bigdecimal_eq(&got, &expected)) {
                char gb[128], eb_[128];
                bigdecimal_to_str(&got, BIGDECIMAL_FMT_SCIENTIFIC_E, gb,
                        sizeof(gb), NULL);
                bigdecimal_to_str(&expected, BIGDECIMAL_FMT_SCIENTIFIC_E, eb_,
                        sizeof(eb_), NULL);
                printf("FAIL dec tie p=%zu m_odd=%d: got %s expect %s\n", p,
                        (int)m_odd, gb, eb_);
                g_fail++;
            }
            bigint_dec_free(&exp_mant);
            bigdecimal_free(&expected);
            bigdecimal_free(&got);
        }
    }
    printf("bigdecimal ties: OK\n");
    bigint_dec_free(&one);
    bigint_dec_free(&five);
    bigint_dec_free(&ten);
    bigint_dec_free(&tie);
    bigint_dec_free(&m);
}

int main(void) {
    test_bigfloat_ties();
    test_bigdecimal_ties();

    if (g_fail == 0) {
        printf("test_rounding_tie: ALL PASS\n");
        return 0;
    }
    printf("test_rounding_tie: %d FAILURES\n", g_fail);
    return 1;
}
