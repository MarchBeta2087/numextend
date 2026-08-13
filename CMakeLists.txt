cmake_minimum_required(VERSION 3.16)
project(numextend C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# nex 静态库：各模块源文件与公共算法模块（nex/ntt/），后续按目录追加
add_library(nex STATIC
    nex/nex_alloc.c
    nex/bigint/bin/nex_bigint_bin.c
    nex/bigint/bin/nex_bigint_bin_mul.c
    nex/bigint/bin/nex_bigint_bin_div.c
    nex/bigint/bin/nex_bigint_bin_gcd.c
    nex/bigint/bin64/nex_bigint_bin64.c
    nex/bigint/dec/nex_bigint_dec.c
    nex/bigint/dec/nex_bigint_dec_mul.c
    nex/bigint/dec/nex_bigint_dec_div.c
    nex/bigint/nex_bigint_conv.c
    nex/fft/nex_fft.c
    nex/ntt/nex_ntt.c
    nex/bigfrac/nex_bigfrac.c
    nex/bigfloat/nex_bigfloat.c
    nex/bigfloat/nex_bigfloat_str.c
    nex/bigdecimal/nex_bigdecimal.c
    nex/bigdecimal/nex_bigdecimal_str.c
    nex/bigcomplex/float/nex_bigcomplex_float.c
    nex/bigcomplex/decimal/nex_bigcomplex_decimal.c
    nex/convert/nex_convert.c
)

# 根相对 include（"nex/bigint/nex_bigint_common.h"）以仓库根为基准
target_include_directories(nex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(nex PRIVATE /W4)
else()
    target_compile_options(nex PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_subdirectory(tests)

# 演示程序（可选，仅构建不测试）
add_executable(demo demo/demo.c)
target_link_libraries(demo PRIVATE nex)
target_include_directories(demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 性能基准（可选目标）
add_executable(bench bench/bench.c)
target_link_libraries(bench PRIVATE nex)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 数学库：Linux/macOS 需显式 -lm（库内 floor/sqrt 等；Windows/mingw 的 libm 在 libc 内）
if(NOT WIN32)
    target_link_libraries(nex PUBLIC m)
endif()
