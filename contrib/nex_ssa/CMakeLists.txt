add_library(nex_ssa STATIC nex_ssa.c)
target_link_libraries(nex_ssa PUBLIC nex)
target_include_directories(nex_ssa PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR})
if(MSVC)
    target_compile_options(nex_ssa PRIVATE /W4)
else()
    target_compile_options(nex_ssa PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(nex_ssa_test nex_ssa_test.c)
target_link_libraries(nex_ssa_test PRIVATE nex_ssa)
add_test(NAME nex_ssa_test COMMAND nex_ssa_test)

add_executable(nex_ssa_bench nex_ssa_bench.c)
target_link_libraries(nex_ssa_bench PRIVATE nex_ssa)
if(WIN32)
    target_link_libraries(nex_ssa_bench PRIVATE psapi)
endif()
