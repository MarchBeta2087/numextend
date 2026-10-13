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

# 可选 sanitizer（CI / 本地验证）：-DNEX_SANITIZE=address,undefined
# MSVC 仅支持 address；GCC/Clang 支持组合且遇错即停（halt-on-error）。
set(NEX_SANITIZE "" CACHE STRING "启用 sanitizer（如 address,undefined）")
if(NEX_SANITIZE)
    if(MSVC)
        # MSVC 仅 address（无 UBSan）；运行时经 .drectve 自动链接
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=${NEX_SANITIZE} -fno-omit-frame-pointer
                            -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${NEX_SANITIZE})
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 版本头：与 project(VERSION) 保持单一来源，生成到构建目录
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/nex/nex_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/nex/nex_version.h
    @ONLY)

# nex 库：各模块源文件与公共算法模块（nex/ntt/），后续按目录追加。
# 库类型由 BUILD_SHARED_LIBS 决定（默认 STATIC）。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(BUILD_SHARED_LIBS)
    # MSVC 共享库自动导出全部符号（本库无公开全局数据）
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_library(nex
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

# 共享库版本号：SOVERSION = 主版本号（ABI 版本，见 design.md §1.4）
set_target_properties(nex PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# 根相对 include（"nex/bigint/nex_bigint_common.h"）以仓库根为基准；
# 另加构建目录的 include（生成 nex/nex_version.h）。
# BUILD/INSTALL 接口区分构建树与安装树，供 install(EXPORT) 使用。
target_include_directories(nex PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

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

# ------------------------------------------------------------------
# 安装与导出：find_package(nex CONFIG) + pkg-config
# ------------------------------------------------------------------
install(TARGETS nex
    EXPORT nexTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 公开头文件（排除内部头与 .h.in 模板）
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nex/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nex
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*_internal.h" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/nex/nex_version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nex)

# CMake 包配置（nexConfig.cmake / nexConfigVersion.cmake / nexTargets.cmake）
install(EXPORT nexTargets
    FILE nexTargets.cmake
    NAMESPACE nex::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nex)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nexConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/nexConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nex)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/nexConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/nexConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/nexConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nex)

# pkg-config
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nex.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/nex.pc
    @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nex.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
