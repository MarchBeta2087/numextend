#!/bin/sh
# 覆盖率：GCC/Clang --coverage，跑完整 CTest，再用 gcovr 统计与阈值检查。
# 用法：sh scripts/coverage.sh [阈值]   默认阈值 80（仅行覆盖率，作用于 nex/）
# 若未安装 gcovr，仅构建/测试并输出提示（CI 用 linux-coverage job 的 gcovr）。
set -e
cd "$(dirname "$0")/.."
THRESHOLD="${1:-80}"
CC="${CC:-cc}"

cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_C_FLAGS="--coverage -fprofile-abs-path" \
    -DCMAKE_EXE_LINKER_FLAGS="--coverage" > /dev/null
cmake --build build-cov --parallel
ctest --test-dir build-cov --output-on-failure

if command -v gcovr > /dev/null 2>&1; then
    echo "=== 库行覆盖率（nex/）==="
    gcovr --root . --filter 'nex/' --txt
    gcovr --root . --filter 'nex/' --print-summary \
          --fail-under-line "$THRESHOLD"
else
    echo "gcovr 未安装：跳过阈值检查（apt install gcovr / pip install gcovr）"
fi
