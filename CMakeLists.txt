cmake_minimum_required(VERSION 3.16)
project(numextend VERSION 1.0.0 LANGUAGES C)

# 默认构建类型：未显式指定且非多配置生成器时取 RelWithDebInfo（-O2 -g）。
# 否则 CMake 会以空 CMAKE_BUILD_TYPE 生成 -O0 构建：既严重拖慢性能，
# 又会在基准中扭曲各算法的交叉点（性能对比必须与被对比基线同优化级别）。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 版本头：与 project(VERSION) 保持单一来源，生成到构建目录
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/nex/nex_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/nex/nex_version.h
    @ONLY)

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

# 根相对 include（"nex/bigint/nex_bigint_common.h"）以仓库根为基准；
# 另加构建目录的 include（生成 nex/nex_version.h）
target_include_directories(nex PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include)

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

# 全面性能扫描（可选目标）
add_executable(bench_sweep bench/bench_sweep.c)
target_link_libraries(bench_sweep PRIVATE nex)
target_include_directories(bench_sweep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 数学库：Linux/macOS 需显式 -lm（库内 floor/sqrt 等；Windows/mingw 的 libm 在 libc 内）
if(NOT WIN32)
    target_link_libraries(nex PUBLIC m)
endif()
