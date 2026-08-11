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
endif()
