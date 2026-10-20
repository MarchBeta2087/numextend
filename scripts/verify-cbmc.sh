#!/bin/sh
# P2-C：limb 层有界验证（CBMC，issue #98）。
#
# 对 nex/bigint/bin 的信任基做有界模型检查：输入肢数固定为 0..3（0..96 bit），
# 加/减/比较与独立参考模型逐 digit 一致，乘法检查无 UB/规范化 + 小常数乘子
# 的功能正确性；全部带溢出/越界/指针/移位/循环展开断言。
#
# 用法：
#   sh scripts/verify-cbmc.sh                 # 无 cbmc 时 SKIP（返回 0）
#   NEX_CBMC_REQUIRE=1 sh scripts/verify-cbmc.sh   # 无 cbmc 时失败（CI 用）
#   CBMC=/path/to/cbmc JOBS=8 sh scripts/verify-cbmc.sh
#
# 依赖：CBMC >= 5.95（Debian/Ubuntu 包名 cbmc）。CI job `linux-cbmc`。
set -eu
cd "$(dirname "$0")/.."

CBMC="${CBMC:-cbmc}"
if ! command -v "$CBMC" >/dev/null 2>&1; then
    if [ "${NEX_CBMC_REQUIRE:-0}" = "1" ]; then
        echo "FAIL: 找不到 cbmc（NEX_CBMC_REQUIRE=1）" >&2
        exit 1
    fi
    echo "SKIP: 找不到 cbmc（安装：apt-get install cbmc）"
    exit 0
fi
export CBMC
LOG_DIR="${LOG_DIR:-build-cbmc}"
export LOG_DIR
mkdir -p "$LOG_DIR"

JOBS="${JOBS:-4}"
JOBS_LIST=$(mktemp)
trap 'rm -f "$JOBS_LIST"' EXIT

# 加/减/比较：功能对拍，含等长与不等长（不等长覆盖 mag_add/mag_sub 的
# 尾随进位/借位循环）、零操作数、别名。
PAIRS="0x0 1x1 3x3 1x3 3x1 0x3"
for h in harness_add harness_sub harness_add_alias harness_sub_alias harness_cmp; do
    for p in $PAIRS; do
        echo "$h ${p%x*} ${p#*x} 0 -DDUMMY=0" >> "$JOBS_LIST"
    done
done
# 乘法：安全性质（无 UB/规范化/符号）；不等长同样覆盖尾随进位
for p in $PAIRS; do
    echo "harness_mul_safe ${p%x*} ${p#*x} 1 -DDUMMY=0" >> "$JOBS_LIST"
done
# 小常数乘子 0/1/2/3（等价性在常数操作数下退化为移位/加法，SAT 很快）
for k in 0 1 2 3; do
    echo "harness_mul_const 3 3 1 -DMUL_K=$k" >> "$JOBS_LIST"
done

echo "== CBMC limb 验证：$(wc -l < "$JOBS_LIST") 个目标，并行 $JOBS"
if ! xargs -n 5 -P "$JOBS" sh verify/cbmc/run-one.sh < "$JOBS_LIST"; then
    echo "FAILED：见 $LOG_DIR/*.log" >&2
    exit 1
fi
echo "all CBMC harnesses passed"
