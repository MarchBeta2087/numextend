# tests/：单元测试与黄金对拍

add_executable(test_bigint_bin test_bigint_bin.c)
target_link_libraries(test_bigint_bin PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigint_bin PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigint_bin_unit COMMAND test_bigint_bin)

add_executable(test_bigint_dec test_bigint_dec.c)
target_link_libraries(test_bigint_dec PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigint_dec PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigint_dec_unit COMMAND test_bigint_dec)

add_executable(bigint_bin_golden_driver golden_driver.c)
target_link_libraries(bigint_bin_golden_driver PRIVATE nex)

add_executable(test_bigfrac test_bigfrac.c)
target_link_libraries(test_bigfrac PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigfrac PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigfrac_unit COMMAND test_bigfrac)

add_executable(bigfrac_golden_driver golden_bigfrac_driver.c)
target_link_libraries(bigfrac_golden_driver PRIVATE nex)

add_executable(test_bigfloat test_bigfloat.c)
target_link_libraries(test_bigfloat PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigfloat PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigfloat_unit COMMAND test_bigfloat)

add_executable(test_bigfloat_fuzz test_bigfloat_fuzz.c)
target_link_libraries(test_bigfloat_fuzz PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigfloat_fuzz PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigfloat_fuzz COMMAND test_bigfloat_fuzz)

add_executable(test_rounding_tie test_rounding_tie.c)
target_link_libraries(test_rounding_tie PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_rounding_tie PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME rounding_tie COMMAND test_rounding_tie)

add_executable(test_oom test_oom.c)
target_link_libraries(test_oom PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_oom PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME oom_injection COMMAND test_oom)

add_executable(bigfloat_golden_driver golden_bigfloat_driver.c)
target_link_libraries(bigfloat_golden_driver PRIVATE nex)

add_executable(test_bigdecimal test_bigdecimal.c)
target_link_libraries(test_bigdecimal PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigdecimal PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigdecimal_unit COMMAND test_bigdecimal)

add_executable(bigdecimal_golden_driver golden_bigdecimal_driver.c)
target_link_libraries(bigdecimal_golden_driver PRIVATE nex)

add_executable(test_bigcomplex_float test_bigcomplex_float.c)
target_link_libraries(test_bigcomplex_float PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigcomplex_float PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigcomplex_float_unit COMMAND test_bigcomplex_float)

add_executable(test_bigcomplex_decimal test_bigcomplex_decimal.c)
target_link_libraries(test_bigcomplex_decimal PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_bigcomplex_decimal PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME bigcomplex_decimal_unit COMMAND test_bigcomplex_decimal)

add_executable(bigcomplex_float_golden_driver golden_bigcomplex_float_driver.c)
target_link_libraries(bigcomplex_float_golden_driver PRIVATE nex)

add_executable(bigcomplex_decimal_golden_driver golden_bigcomplex_decimal_driver.c)
target_link_libraries(bigcomplex_decimal_golden_driver PRIVATE nex)

add_executable(test_convert test_convert.c)
target_link_libraries(test_convert PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_convert PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME convert_unit COMMAND test_convert)

add_executable(convert_golden_driver golden_convert_driver.c)
target_link_libraries(convert_golden_driver PRIVATE nex)

add_executable(test_ntt test_ntt.c)
target_link_libraries(test_ntt PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_ntt PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME ntt_unit COMMAND test_ntt)

add_executable(test_ntt_mul test_ntt_mul.c)
target_link_libraries(test_ntt_mul PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_ntt_mul PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME ntt_mul_unit COMMAND test_ntt_mul)

add_executable(test_fft test_fft.c)
target_link_libraries(test_fft PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_fft PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME fft_unit COMMAND test_fft)

add_executable(test_fft_mul test_fft_mul.c)
target_link_libraries(test_fft_mul PRIVATE nex)
if(NOT MSVC)
    target_compile_options(test_fft_mul PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME fft_mul_unit COMMAND test_fft_mul)

# Python 可用时注册黄金对拍；脚本自行定位驱动可执行文件
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME bigint_bin_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigint_bin.py
                $<TARGET_FILE:bigint_bin_golden_driver>)
    # dec 对拍 + bin↔dec 互转交叉验证（同一驱动，d_/c_ 前缀命令）
    add_test(NAME bigint_dec_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigint_dec.py
                $<TARGET_FILE:bigint_bin_golden_driver>)
    add_test(NAME bigfrac_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigfrac.py
                $<TARGET_FILE:bigfrac_golden_driver>)
    add_test(NAME bigfloat_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigfloat.py
                $<TARGET_FILE:bigfloat_golden_driver>)
    add_test(NAME bigdecimal_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigdecimal.py
                $<TARGET_FILE:bigdecimal_golden_driver>)
    add_test(NAME bigcomplex_float_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigcomplex_float.py
                $<TARGET_FILE:bigcomplex_float_golden_driver>)
    add_test(NAME bigcomplex_decimal_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigcomplex_decimal.py
                $<TARGET_FILE:bigcomplex_decimal_golden_driver>)
    add_test(NAME convert_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_convert.py
                $<TARGET_FILE:convert_golden_driver>)
    # sqrt 独立参考（精确边界比较法，覆盖五种舍入模式，弥补同构复刻盲区）
    add_test(NAME sqrt_golden
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/golden_sqrt.py
                $<TARGET_FILE:bigfloat_golden_driver>
                $<TARGET_FILE:bigdecimal_golden_driver>)
    # 多种子常规化：每种黄金对拍以额外种子再跑两遍（NEX_GOLDEN_SEED），
    # 捕捉种子相关的不稳定用例（设计 §12 随机验证的常规执行）
    foreach(seed IN ITEMS 1 2)
        add_test(NAME bigfloat_golden_seed${seed}
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigfloat.py
                    $<TARGET_FILE:bigfloat_golden_driver>)
        set_tests_properties(bigfloat_golden_seed${seed} PROPERTIES
            ENVIRONMENT "NEX_GOLDEN_SEED=${seed}")
        add_test(NAME bigdecimal_golden_seed${seed}
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/golden_bigdecimal.py
                    $<TARGET_FILE:bigdecimal_golden_driver>)
        set_tests_properties(bigdecimal_golden_seed${seed} PROPERTIES
            ENVIRONMENT "NEX_GOLDEN_SEED=${seed}")
        add_test(NAME convert_golden_seed${seed}
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/golden_convert.py
                    $<TARGET_FILE:convert_golden_driver>)
        set_tests_properties(convert_golden_seed${seed} PROPERTIES
            ENVIRONMENT "NEX_GOLDEN_SEED=${seed}")
    endforeach()
endif()
