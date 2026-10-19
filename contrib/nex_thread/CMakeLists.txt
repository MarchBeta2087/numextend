# contrib/nex_thread：跨平台线程池（TODO §F 隔离实验，默认不构建）。
add_library(nex_thread STATIC nex_thread.c)
target_include_directories(nex_thread PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nex_thread PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(nex_thread PRIVATE /W4)
else()
    target_compile_options(nex_thread PRIVATE -Wall -Wextra -Wpedantic)
endif()
