/*
 * nex_ooc_test：out-of-core 分块乘法的对拍测试（P1b-OOC M1，issue #104）。
 *
 * 覆盖：单块/多块、非对称长度、块对进位链（含全 0xFF 触发长进位）、
 * 零与符号、块长边界（1 肢块）、结果规范化与可重复性。
 * 参考实现为内存内 `bigint_bin_mul`，要求逐位一致。
 */

#include "nex_ooc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c) do { \
    if (!(c)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        g_fail++; \
    } \
} while (0)

static uint64_t g_rng = 0x243F6A8885A308D3ULL;

static uint32_t rnd32(void)
{
    g_rng ^= g_rng << 13U;
    g_rng ^= g_rng >> 7U;
    g_rng ^= g_rng << 17U;
    return (uint32_t)(g_rng >> 16);
}

static void fill_random(bigint_bin_ty *v, size_t limbs)
{
    size_t i;
    (void)bigint_bin_free(v);
    if (bigint_bin_init_cap(v, (limbs == 0U) ? 1U : limbs) != BIGINT_OK_E) {
        exit(2);
    }
    for (i = 0U; i < limbs; i++) {
        v->limbs[i] = rnd32();
    }
    v->len = limbs;
    v->sign = (limbs == 0U) ? BIGINT_SIGN_ZERO_E : BIGINT_SIGN_POS_E;
    while ((v->len > 0U) && (v->limbs[v->len - 1U] == 0U)) {
        v->len--;
    }
    if (v->len == 0U) {
        v->limbs[0] = 1U;
        v->len = 1U;
        v->sign = BIGINT_SIGN_POS_E;
    }
}

static void fill_ones(bigint_bin_ty *v, size_t limbs)
{
    size_t i;
    (void)bigint_bin_free(v);
    if (bigint_bin_init_cap(v, limbs) != BIGINT_OK_E) {
        exit(2);
    }
    for (i = 0U; i < limbs; i++) {
        v->limbs[i] = 0xFFFFFFFFU;
    }
    v->len = limbs;
    v->sign = BIGINT_SIGN_POS_E;
}

/* 返回 0 成功；打印一行摘要 */
static int check_case(const char *name, size_t a_limbs, size_t b_limbs,
        size_t block, int negate_a, int negate_b, int ones)
{
    bigint_bin_ty a;
    bigint_bin_ty b;
    bigint_bin_ty want;
    bigint_bin_ty got;
    bigint_bin_ty again;
    nex_ooc_opts_ty opts;
    nex_ooc_stats_ty stats;
    nex_ooc_err_ty err;
    int rc = 1;

    (void)bigint_bin_init(&a);
    (void)bigint_bin_init(&b);
    (void)bigint_bin_init(&want);
    (void)bigint_bin_init(&got);
    (void)bigint_bin_init(&again);

    if (ones != 0) {
        if (a_limbs != 0U) {
            fill_ones(&a, a_limbs);
        }
        if (b_limbs != 0U) {
            fill_ones(&b, b_limbs);
        }
    } else {
        if (a_limbs != 0U) {
            fill_random(&a, a_limbs);
        }
        if (b_limbs != 0U) {
            fill_random(&b, b_limbs);
        }
    }
    if (a.len > 0U) {
        a.sign = negate_a ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }
    if (b.len > 0U) {
        b.sign = negate_b ? BIGINT_SIGN_NEG_E : BIGINT_SIGN_POS_E;
    }

    if (bigint_bin_mul(&want, &a, &b) != BIGINT_OK_E) {
        printf("FAIL %s: reference mul failed\n", name);
        goto out;
    }

    (void)memset(&opts, 0, sizeof(opts));
    opts.block_limbs = block;
    (void)memset(&stats, 0, sizeof(stats));
    err = nex_ooc_bin_mul(&got, &a, &b, &opts, &stats);
    if (err != NEX_OOC_OK_E) {
        printf("FAIL %s: ooc mul failed (%s)\n", name,
                nex_ooc_strerror(err));
        goto out;
    }
    if (bigint_bin_cmp(&got, &want) != 0) {
        printf("  [dbg] a.len=%zu b.len=%zu block=%zu pairs=%zu res=%llu rd=%llu wr=%llu\n",
                a.len, b.len, stats.block_limbs, stats.block_pairs,
                (unsigned long long)stats.result_limbs,
                (unsigned long long)stats.bytes_read,
                (unsigned long long)stats.bytes_written);
        printf("FAIL %s: mismatch (a=%zu b=%zu block=%zu got.len=%zu "
                "want.len=%zu)\n", name, a_limbs, b_limbs, block,
                got.len, want.len);
        /* 定位首个不同的肢，便于排查进位链 */
        {
            const size_t n = (got.len > want.len) ? got.len : want.len;
            size_t i;
            for (i = 0U; i < n; i++) {
                const uint32_t g = (i < got.len) ? got.limbs[i] : 0U;
                const uint32_t w = (i < want.len) ? want.limbs[i] : 0U;
                if (g != w) {
                    printf("  first diff at limb %zu: got=%08x want=%08x\n",
                            i, (unsigned)g, (unsigned)w);
                    break;
                }
            }
        }
        goto out;
    }

    /* 结果肢数与块规划自洽 */
    if (stats.result_limbs != got.len) {
        printf("FAIL %s: stats.result_limbs=%llu != len=%zu\n", name,
                (unsigned long long)stats.result_limbs, got.len);
        goto out;
    }
    if (stats.block_pairs != stats.blocks_a * stats.blocks_b) {
        printf("FAIL %s: block_pairs=%zu != %zu*%zu\n", name,
                stats.block_pairs, stats.blocks_a, stats.blocks_b);
        goto out;
    }

    /* 可重复性：同参数再跑一次，结果完全一致 */
    err = nex_ooc_bin_mul(&again, &a, &b, &opts, NULL);
    if ((err != NEX_OOC_OK_E) || (bigint_bin_cmp(&again, &got) != 0)) {
        printf("FAIL %s: not reproducible\n", name);
        goto out;
    }

    printf("ok %-22s a=%-7zu b=%-7zu block=%-6zu pairs=%-4zu "
            "result=%-8zu %.3f s\n", name, a_limbs, b_limbs,
            stats.block_limbs, stats.block_pairs, stats.result_limbs,
            stats.seconds);
    rc = 0;

out:
    bigint_bin_free(&again);
    bigint_bin_free(&got);
    bigint_bin_free(&want);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
    return rc;
}

int main(void)
{
    /* 单块、块边界、跨块进位 */
    if (check_case("single limb", 1U, 1U, 8U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("below block", 7U, 5U, 8U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("exact block", 8U, 8U, 8U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("block+1", 9U, 8U, 8U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("asym 33x17", 33U, 17U, 8U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("1-limb blocks", 37U, 23U, 1U, 0, 0, 0) != 0) {
        return 1;
    }
    /* 全 1：最长进位链 */
    if (check_case("ones 1-limb blocks", 64U, 64U, 1U, 0, 0, 1) != 0) {
        return 1;
    }
    if (check_case("ones 130x130", 130U, 130U, 16U, 0, 0, 1) != 0) {
        return 1;
    }
    /* 非对称：a 多块、b 单块（走 schoolbook 分支） */
    if (check_case("wide x narrow", 6400U, 65U, 64U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("narrow x wide", 65U, 6400U, 64U, 0, 0, 0) != 0) {
        return 1;
    }
    /* 中等规模 */
    if (check_case("mid 1000x999", 1000U, 999U, 64U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("mid 5000x1234", 5000U, 1234U, 1000U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("mid 20000x20000", 20000U, 20000U, 4096U, 0, 0, 0) != 0) {
        return 1;
    }
    /* 符号：结果符号按常规规则 */
    if (check_case("neg x pos", 300U, 200U, 32U, 1, 0, 0) != 0) {
        return 1;
    }
    if (check_case("neg x neg", 300U, 200U, 32U, 1, 1, 0) != 0) {
        return 1;
    }
    /* 零操作数 */
    if (check_case("zero x value", 0U, 300U, 32U, 0, 0, 0) != 0) {
        return 1;
    }
    if (check_case("zero x zero", 0U, 0U, 32U, 0, 0, 0) != 0) {
        return 1;
    }
    /* 默认块长（2^23）下的小输入 */
    if (check_case("default block", 200U, 300U, 0U, 0, 0, 0) != 0) {
        return 1;
    }
    /* 非法参数 */
    {
        bigint_bin_ty a;
        bigint_bin_ty b;
        (void)bigint_bin_init(&a);
        (void)bigint_bin_init(&b);
        fill_random(&a, 10U);
        fill_random(&b, 10U);
        CHECK(nex_ooc_bin_mul(NULL, &a, &b, NULL, NULL)
                == NEX_OOC_ERR_INVALID_E);
        CHECK(nex_ooc_bin_mul(&a, NULL, &b, NULL, NULL)
                == NEX_OOC_ERR_INVALID_E);
        CHECK(nex_ooc_mul_files(NULL, "x", 1U, "y", 1U, NULL, NULL)
                == NEX_OOC_ERR_INVALID_E);
        bigint_bin_free(&b);
        bigint_bin_free(&a);
    }

    if (g_fail != 0) {
        printf("%d failure(s)\n", g_fail);
        return 1;
    }
    printf("all out-of-core tests passed\n");
    return 0;
}
