cmake_minimum_required(VERSION 3.16)
project(numextend VERSION 1.1.0 LANGUAGES C)

# 默认构建类型：未显式指定且非多配置生成器时取 RelWithDebInfo（-O2 -g）。
# 否则 CMake 会以空 CMAKE_BUILD_TYPE 生成 -O0 构建：既严重拖慢性能，
# 又会在基准中扭曲各算法的交叉点（性能对比必须与被对比基线同优化级别）。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# C 标准：默认 C99（项目承诺基准，含 coding_standard.md §1.1）。
# NEX_C_STANDARD 可覆盖为 11/17/23（/2y）以验证标准矩阵（TODO §A3）。
set(NEX_C_STANDARD "99" CACHE STRING "C 标准版本（99/11/17/23/2y）")
set(CMAKE_C_STANDARD ${NEX_C_STANDARD})
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

# 模块清单与依赖闭包（TODO §B1/B2）：数据源 cmake/nex_modules.txt。
# NEX_MODULES=all 构建全部模块；否则按依赖闭包裁剪（文件级子集）。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/nex_modules.cmake)
set(NEX_MODULES "all" CACHE STRING
    "构建的模块：all，或逗号/分号分隔的模块名（TODO §B2）")
if(NEX_MODULES STREQUAL "all")
    set(_nex_requested ${NEX_MODULE_LIST})
else()
    string(REPLACE "," ";" _nex_requested "${NEX_MODULES}")
endif()
nex_resolve_modules(NEX_SOURCES NEX_PUBLIC_HEADERS NEX_RESOLVED_MODULES
    "${_nex_requested}")
list(LENGTH NEX_SOURCES _nex_nsrc)
message(STATUS "nex modules: ${NEX_RESOLVED_MODULES}")
message(STATUS "nex sources (incl. deps): ${_nex_nsrc}")

# 库类型由 BUILD_SHARED_LIBS 决定（默认 STATIC）。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(BUILD_SHARED_LIBS)
    # MSVC 共享库自动导出全部符号（本库无公开全局数据）
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_library(nex ${NEX_SOURCES})

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

# 测试 / 演示 / 基准：仅完整构建（子集构建不含全量测试）。
if(NEX_MODULES STREQUAL "all")
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
elseif("bigint_bin" IN_LIST NEX_RESOLVED_MODULES)
    # 子集构建：最小冒烟测试（仅依赖 bigint_bin 闭包）。
    add_executable(subset_smoke tests/subset_smoke.c)
    target_link_libraries(subset_smoke PRIVATE nex)
    target_include_directories(subset_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

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

# 公开头文件：仅安装依赖闭包内的头（保持 nex/ 下的相对路径）。
foreach(_h IN LISTS NEX_PUBLIC_HEADERS)
    file(RELATIVE_PATH _rel
        "${CMAKE_CURRENT_SOURCE_DIR}/nex"
        "${CMAKE_CURRENT_SOURCE_DIR}/${_h}")
    get_filename_component(_rel_dir "${_rel}" DIRECTORY)
    if(_rel_dir STREQUAL "")
        set(_dst "${CMAKE_INSTALL_INCLUDEDIR}/nex")
    else()
        set(_dst "${CMAKE_INSTALL_INCLUDEDIR}/nex/${_rel_dir}")
    endif()
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_h}" DESTINATION "${_dst}")
endforeach()
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
