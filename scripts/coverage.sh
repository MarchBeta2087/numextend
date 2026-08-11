#!/bin/sh
# 覆盖率统计（GCC --coverage + gcov）：构建覆盖率版 → 跑全部测试与黄金
# 对拍 → 输出各模块行覆盖率。用法：sh scripts/coverage.sh
set -e
GCC_BIN=$(dirname "$(command -v gcc 2>/dev/null)" 2>/dev/null || echo /c/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r1/mingw64/bin)
GCC="$GCC_BIN/gcc.exe"
GCOV="$GCC_BIN/gcov.exe"
cd "$(dirname "$0")/.."
cmake -S . -B build-cov -DCMAKE_C_FLAGS="-fprofile-arcs -ftest-coverage" \
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-arcs" > /dev/null
cmake --build build-cov > /dev/null 2>&1
cd build-cov/tests
for t in test_bigint_bin test_bigint_dec test_bigfrac test_bigfloat \
         test_bigfloat_fuzz test_rounding_tie test_bigdecimal \
         test_bigcomplex_float test_bigcomplex_decimal test_convert test_oom; do
    ./$t.exe > /dev/null 2>&1 || true
done
cd ../..
for g in bigint_bin bigint_dec bigfrac bigfloat bigdecimal \
         bigcomplex_float bigcomplex_decimal convert; do
    /c/Python313/python tests/golden_$g.py \
        build-cov/tests/${g}_golden_driver.exe > /dev/null 2>&1 || true
done
/c/Python313/python tests/golden_bigint_dec.py \
    build-cov/tests/bigint_bin_golden_driver.exe > /dev/null 2>&1 || true
/c/Python313/python tests/golden_sqrt.py \
    build-cov/tests/bigfloat_golden_driver.exe \
    build-cov/tests/bigdecimal_golden_driver.exe > /dev/null 2>&1 || true
total=0; total_l=0
echo "=== 库行覆盖率 ==="
for f in $(find build-cov/CMakeFiles/nex.dir -name "*.gcno" | sort); do
    r=$("$GCOV" -o "$(dirname "$f")" "$f" 2>/dev/null \
        | grep "Lines executed" | grep -oE "[0-9.]+% of [0-9]+" | head -1)
    pct=${r%%%*}; lines=${r##*of }; pctv=$(echo "$pct" | cut -d. -f1)
    total=$((total + pctv * ${lines// /})); total_l=$((total_l + ${lines// /}))
    printf "%-44s %s\n" "$(basename "$f" .c.gcno)" "$r"
done
echo "---"
echo "加权平均: $((total / total_l))% ($total_l 行)"
