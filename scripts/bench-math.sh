#!/bin/sh
# 数学专项基准：超越函数 / 常数多表达式 / 吞吐量（TODO §J10）。
# 用法：sh scripts/bench-math.sh [full|quick|ci] [sections] [预算秒]
#   sections：all（默认）或 trans,const,throughput 的逗号组合。
# 预算缺省：full 7200、quick 600、ci 300。
# 输出：build-bench/bench-math-<preset>.txt 与 .json（JSON Lines）。
set -e
cd "$(dirname "$0")/.."

PRESET="${1:-full}"
SECTIONS="${2:-all}"
BUDGET="${3:-}"
CC="${CC:-cc}"
BUILD=build-bench

case "$PRESET" in
    full)  DEF=7200 ;;
    quick) DEF=600 ;;
    ci)    DEF=300 ;;
    *)     echo "未知预设: $PRESET（full|quick|ci）" >&2; exit 2 ;;
esac
if [ -z "$BUDGET" ]; then
    BUDGET="$DEF"
fi

GEN=""
if command -v ninja > /dev/null 2>&1; then
    GEN="-G Ninja"
fi

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release $GEN \
    -DCMAKE_C_COMPILER="$CC" > /dev/null
cmake --build "$BUILD" --target bench_math --parallel

if [ -x "$BUILD/bench_math" ]; then
    BIN="$BUILD/bench_math"
else
    BIN="$BUILD/bench_math.exe"
fi

OUT="$BUILD/bench-math-$PRESET"
echo "运行 $BIN --preset $PRESET --sections $SECTIONS --budget $BUDGET"
"$BIN" --preset "$PRESET" --sections "$SECTIONS" --budget "$BUDGET" \
    --json "$OUT.json" | tee "$OUT.txt"
echo "已写入 $OUT.txt 与 $OUT.json"
