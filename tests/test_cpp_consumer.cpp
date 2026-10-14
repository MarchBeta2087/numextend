/*
 * test_cpp_consumer.cpp：C++ 互操作测试（TODO §A1 / §A2）。
 *
 * 目的：验证全部公开头文件均可被 C++ 编译单元包含，且能链接 C 编译的
 * libnex（依赖各头文件的 extern "C" 守卫）。删除任一守卫后，本测试会因
 * name mangling 在链接期失败。
 *
 * 覆盖：七类型 + bin64 + fft/ntt 的符号解析，以及一次 bigint 实数运算。
 * 用 -DCMAKE_CXX_STANDARD=11/17/23 可复用本文件验证多个 C++ 标准。
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "nex/bigint/nex_bigint_common.h"
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/bigint/bin64/nex_bigint_bin64.h"
#include "nex/bigint/dec/nex_bigint_dec.h"
#include "nex/bigint/nex_bigint_conv.h"
#include "nex/bigfrac/nex_bigfrac.h"
#include "nex/bigfloat/nex_bigfloat.h"
#include "nex/bigdecimal/nex_bigdecimal.h"
#include "nex/bigcomplex/float/nex_bigcomplex_float.h"
#include "nex/bigcomplex/decimal/nex_bigcomplex_decimal.h"
#include "nex/convert/nex_convert.h"
#include "nex/fft/nex_fft.h"
#include "nex/ntt/nex_ntt.h"
#include "nex/nex_alloc.h"
#include "nex/nex_version.h"

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures; \
        } \
    } while (0)

int main() {
    CHECK(NEX_VERSION_MAJOR >= 1);

    /* bigint_bin：真实运算 + 字符串输出 */
    bigint_bin_ty a{};
    bigint_bin_ty b{};
    bigint_bin_ty sum{};
    CHECK(bigint_bin_init(&a) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&b) == BIGINT_OK_E);
    CHECK(bigint_bin_init(&sum) == BIGINT_OK_E);
    CHECK(bigint_bin_from_u64(&a, 12345u) == BIGINT_OK_E);
    CHECK(bigint_bin_from_u64(&b, 6789u) == BIGINT_OK_E);
    CHECK(bigint_bin_add(&sum, &a, &b) == BIGINT_OK_E);

    char buf[64] = {0};
    size_t needed = 0;
    CHECK(bigint_bin_to_str(&sum, 10, buf, sizeof(buf), &needed) == BIGINT_OK_E);
    CHECK(std::strcmp(buf, "19134") == 0);

    /* convert：bin → frac（精确），验证分子等于源值 */
    bigfrac_ty frac{};
    CHECK(bigfrac_init(&frac) == BIGFRAC_OK_E);
    CHECK(nex_convert_bin_to_frac(&frac, &sum) == NEX_CONVERT_OK_E);
    CHECK(bigint_bin_cmp(bigfrac_num(&frac), &sum) == 0);

    bigfrac_free(&frac);
    bigint_bin_free(&sum);
    bigint_bin_free(&b);
    bigint_bin_free(&a);

    /* 其余类型：init/free（强制链接各自符号） */
    bigint_dec_ty dec{};
    bigint_bin64_ty b64{};
    bigfloat_ty bf{};
    bigdecimal_ty bd{};
    bigcomplex_float_ty cf{};
    bigcomplex_decimal_ty cd{};
    CHECK(bigint_dec_init(&dec) == BIGINT_OK_E);
    CHECK(bigint_bin64_init(&b64) == BIGINT_OK_E);
    CHECK(bigfloat_init(&bf) == BIGFLOAT_OK_E);
    CHECK(bigdecimal_init(&bd) == BIGDECIMAL_OK_E);
    CHECK(bigcomplex_float_init(&cf) == BIGCOMPLEX_FLOAT_OK_E);
    CHECK(bigcomplex_decimal_init(&cd) == BIGCOMPLEX_DECIMAL_OK_E);
    bigint_dec_free(&dec);
    bigint_bin64_free(&b64);
    bigfloat_free(&bf);
    bigdecimal_free(&bd);
    bigcomplex_float_free(&cf);
    bigcomplex_decimal_free(&cd);

    /* fft / ntt：公共算法模块符号。
     * FFT 系数为交错复数组：N = 2^log2n 个复数 = 2N 个 double。 */
    double coeffs[16] = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    CHECK(fft_forward(coeffs, 3) == FFT_OK_E);
    CHECK(ntt_mod_count() > 0u);
    CHECK(ntt_mod_get(0) != nullptr);

    if (failures == 0) {
        std::printf("C++ consumer OK (nex %d.%d.%d)\n",
                NEX_VERSION_MAJOR, NEX_VERSION_MINOR, NEX_VERSION_PATCH);
    }
    return failures == 0 ? 0 : 1;
}
