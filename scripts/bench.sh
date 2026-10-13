#!/bin/sh
# 生成性能报告（Markdown）：Release 构建 + bench + bench_sweep deep + 环境信息。
# 用法：sh scripts/bench.sh [输出文件] [预算秒]
#   默认 docs/performance.md 与 1200 秒（约 20 分钟）。
# 说明：绝对耗时依赖机器与编译器；报告随附环境信息，且必须 -O2/-O3 构建。
set -e
cd "$(dirname "$0")/.."
OUT="${1:-docs/performance.md}"
BUDGET="${2:-1200}"
CC="${CC:-cc}"
BUILD=build-bench

GEN=""
if command -v ninja > /dev/null 2>&1; then
    GEN="-G Ninja"
fi
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release $GEN \
    -DCMAKE_C_COMPILER="$CC" > /dev/null
cmake --build "$BUILD" --target bench bench_sweep --parallel

# 可执行文件后缀（Windows）
if [ -x "$BUILD/bench" ]; then
    BENCH="$BUILD/bench"; SWEEP="$BUILD/bench_sweep"
else
    BENCH="$BUILD/bench.exe"; SWEEP="$BUILD/bench_sweep.exe"
fi

# 环境信息
OS="$(uname -srm 2>/dev/null || echo unknown)"
if [ -r /proc/cpuinfo ]; then
    CPU="$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
elif command -v sysctl > /dev/null 2>&1 && [ "$(uname -s)" = "Darwin" ]; then
    CPU="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
else
    CPU="unknown"
fi
CORES="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo '?')"
CCV="$("$CC" --version 2>/dev/null | head -1)"
DATE="$(date -u '+%Y-%m-%d %H:%M UTC')"

{
    echo "# 性能报告"
    echo
    echo "> 由 \`sh scripts/bench.sh\` 生成。确定性随机输入，各测量点取多次最小值；"
    echo "> 交错采样以抑制热漂移。绝对耗时依赖机器/编译器，务必以同一环境横向比较。"
    echo
    echo "| 项目 | 值 |"
    echo "|---|---|"
    echo "| 生成时间 | $DATE |"
    echo "| 操作系统 | $OS |"
    echo "| CPU | $CPU |"
    echo "| 核心数 | $CORES |"
    echo "| 编译器 | $CCV |"
    echo "| 优化级别 | Release（-O2/-O3，NDEBUG） |"
    echo "| 时间预算 | ${BUDGET}s（deep 档） |"
    echo
    echo "## 固定规模冒烟（bench，单位 ms）"
    echo
    echo '```'
    "$BENCH" 3
    echo '```'
    echo
    echo "## 规模扫描（bench_sweep deep，单位 ms）"
    echo
    echo '```'
    "$SWEEP" deep --budget "$BUDGET"
    echo '```'
} > "$OUT"

echo "报告已写入 $OUT"
