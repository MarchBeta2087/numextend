/* 随机 fuzz 测试：bigfloat vs 硬件 double / float（确定性种子）。
 *
 * 历史价值：开发期该测试抓住了 cmp_mag_exp 的指数差比较方向反转——
 * 单元测试与同构黄金对拍均无法覆盖的独立对照（§12"预设 ctx 下与硬件
 * float/double 对照"）。本文件将其固化进常规套件。
 *
 * 覆盖：
 *   1. 2 万随机 double 位模式 from_f64/to_f64 往返（含特殊/极端位模式）；
 *   2. 2 万随机双精度运算（add/sub/mul/div）与 C double 逐位一致，及
 *      bigfloat_cmp 与 double 比较一致——仅在两者表示范围重叠区比较
 *      （bigfloat binary64 ctx 无次正规、下溢 flush，见设计 §7.3）；
 *   3. 10 万随机 binary32 运算与 C float 一致（对照 §12 硬件浮点）。
 *
 * 随机源为固定种子 LCG（确定性可复现，失败即稳定复现）。
 */
#include "nex/bigfloat/nex_bigfloat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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

/* 正常 double（指数字段 1..2046），保证 bigfloat 与 double 可精确互转 */
static double rnd_normal(uint64_t *s) {
    uint64_t bits;
    do {
        bits = rnd64(s);
        const uint64_t expf = (bits >> 52) & 0x7FF;
        if (expf == 0) {
            bits = (bits & 0x800FFFFFFFFFFFFFULL) | (1ULL << 52);
        }
        if (expf == 0x7FF) {
            bits &= 0x800FFFFFFFFFFFFFULL;
        }
    } while ((((bits >> 52) & 0x7FF) == 0x7FF));
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static void test_f64_roundtrip(void) {
    uint64_t seed = 0x123456789abcdef0ULL;
    bigfloat_ty a;
    bigfloat_init(&a);
    for (int i = 0; i < 20000; i++) {
        uint64_t bits = rnd64(&seed);
        if ((i % 7) == 0) {
            bits &= 0x7FF0000000000000ULL;  // 特殊/极端位模式
        }
        double d;
        memcpy(&d, &bits, 8);
        CHECK(bigfloat_from_f64(&a, d) == BIGFLOAT_OK_E);
        double out = 0.0;
        CHECK(bigfloat_to_f64(&a, &out) == BIGFLOAT_OK_E);
        if (isnan(d)) {
            CHECK(isnan(out));
        } else if (d == 0.0) {
            CHECK(out == 0.0 && (signbit(d) == signbit(out)));
        } else {
            CHECK(out == d);
        }
    }
    printf("f64 roundtrip: 20000 OK\n");
    bigfloat_free(&a);
}

static void test_arith_double(void) {
    uint64_t seed = 0xdeadbeef12345678ULL;
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary64();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);
    for (int i = 0; i < 20000; i++) {
        const double x = rnd_normal(&seed);
        const double y = rnd_normal(&seed);
        bigfloat_from_f64(&a, x);
        bigfloat_from_f64(&b, y);
        double o = 0.0;
        const double r_add = x + y, r_sub = x - y, r_mul = x * y,
                     r_div = x / y;
        /* 仅比较两者表示范围重叠且结果正常的用例（bigfloat binary64
         * ctx 的 flush 边界为 2^-972，double 为 2^-1022，取安全区间） */
        const int guard = !isinf(r_add) && !isinf(r_sub) && !isinf(r_mul)
                && !isinf(r_div) && !isnan(r_add) && !isnan(r_sub)
                && !isnan(r_mul) && !isnan(r_div)
                && fabs(r_add) >= 2e-292 && fabs(r_sub) >= 2e-292
                && fabs(r_mul) >= 2e-292 && fabs(r_div) >= 2e-292
                && fabs(r_add) < 1e307 && fabs(r_sub) < 1e307
                && fabs(r_mul) < 1e307 && fabs(r_div) < 1e307;
        if (guard) {
            bigfloat_add(&c, &a, &b, &ctx);
            bigfloat_to_f64(&c, &o);
            CHECK(o == r_add);
            bigfloat_sub(&c, &a, &b, &ctx);
            bigfloat_to_f64(&c, &o);
            CHECK(o == r_sub);
            bigfloat_mul(&c, &a, &b, &ctx);
            bigfloat_to_f64(&c, &o);
            CHECK(o == r_mul);
            if (y != 0.0) {
                bigfloat_div(&c, &a, &b, &ctx);
                bigfloat_to_f64(&c, &o);
                CHECK(o == r_div);
            }
        }
        /* cmp：无范围限制（from_f64 精确，比较应恒与 double 一致） */
        const int r1 = bigfloat_cmp(&a, &b);
        CHECK((r1 < 0) == (x < y) && (r1 > 0) == (x > y)
                && (r1 == 0) == (x == y));
    }
    printf("arith vs double: 20000 OK\n");
    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

/* 随机正常 float（指数字段 1..254） */
static float rnd_f32(uint64_t *s) {
    uint32_t bits;
    do {
        bits = (uint32_t)(rnd64(s) >> 32);
    } while ((((bits >> 23) & 0xFF) == 0xFF)
            || (((bits >> 23) & 0xFF) == 0));
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static void test_arith_float(void) {
    uint64_t seed = 0xabcdef0123456789ULL;
    const bigfloat_ctx_ty ctx = bigfloat_ctx_binary32();
    bigfloat_ty a;
    bigfloat_ty b;
    bigfloat_ty c;
    bigfloat_init(&a);
    bigfloat_init(&b);
    bigfloat_init(&c);
    for (int i = 0; i < 100000; i++) {
        const float x = rnd_f32(&seed);
        const float y = rnd_f32(&seed);
        const float r_add = x + y, r_mul = x * y;
        if (isinf(r_add) || isinf(r_mul) || isnan(r_add) || isnan(r_mul)) {
            continue;
        }
        /* binary32 ctx flush 边界 = 2^(23−128) = 2^-105 ≈ 2.5e-32 */
        if (fabsf(r_add) < 1e-30f || fabsf(r_mul) < 1e-30f
                || fabsf(r_add) > 1e37f || fabsf(r_mul) > 1e37f) {
            continue;
        }
        bigfloat_from_f64(&a, (double)x);
        bigfloat_from_f64(&b, (double)y);
        double od = 0.0;
        bigfloat_add(&c, &a, &b, &ctx);
        bigfloat_to_f64(&c, &od);
        CHECK((float)od == r_add);
        bigfloat_mul(&c, &a, &b, &ctx);
        bigfloat_to_f64(&c, &od);
        CHECK((float)od == r_mul);
    }
    printf("arith vs float: 100000 OK\n");
    bigfloat_free(&c);
    bigfloat_free(&b);
    bigfloat_free(&a);
}

int main(void) {
    test_f64_roundtrip();
    test_arith_double();
    test_arith_float();

    if (g_fail == 0) {
        printf("test_bigfloat_fuzz: ALL PASS\n");
        return 0;
    }
    printf("test_bigfloat_fuzz: %d FAILURES\n", g_fail);
    return 1;
}
