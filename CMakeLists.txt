cmake_minimum_required(VERSION 3.16)
project(numextend C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# nex 静态库：bigint_bin / bigint_dec 子模块与 bin↔dec 互转单元，
# 后续模块按目录追加
add_library(nex STATIC
    nex/nex_alloc.c
    nex/bigint/bin/nex_bigint_bin.c
    nex/bigint/bin/nex_bigint_bin_mul.c
    nex/bigint/bin/nex_bigint_bin_div.c
    nex/bigint/bin/nex_bigint_bin_gcd.c
    nex/bigint/dec/nex_bigint_dec.c
    nex/bigint/dec/nex_bigint_dec_mul.c
    nex/bigint/dec/nex_bigint_dec_div.c
    nex/bigint/nex_bigint_conv.c
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
