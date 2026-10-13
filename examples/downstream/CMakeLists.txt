cmake_minimum_required(VERSION 3.16)
project(nex_downstream C)

# 独立下游示例：仅通过已安装的 nex 包消费，验证 find_package 与安装布局。
#   cmake -S examples/downstream -B build-downstream \
#         -DCMAKE_PREFIX_PATH=<nex 安装前缀>
#   cmake --build build-downstream
find_package(nex CONFIG REQUIRED)

add_executable(downstream main.c)
target_link_libraries(downstream PRIVATE nex::nex)
