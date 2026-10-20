#!/bin/sh
# 单个 CBMC 目标（由 scripts/verify-cbmc.sh 调用）。
# 用法：run-one.sh <harness> <limbs_a> <limbs_b> <with_mul:0|1> [额外 -D...]
set -eu
cd "$(dirname "$0")/../.."

HARNESS_FN="$1"
NA="$2"
NB="$3"
WITH_MUL="$4"
EXTRA="${5:-}"

: "${CBMC:=cbmc}"
LOG_DIR="${LOG_DIR:-build-cbmc}"
mkdir -p "$LOG_DIR"

HARNESS="verify/cbmc/harness_bigint_bin.c"
SRC="nex/bigint/bin/nex_bigint_bin.c nex/nex_alloc.c"
if [ "$WITH_MUL" = "1" ]; then
    SRC="$SRC nex/bigint/bin/nex_bigint_bin_mul.c"
fi

MAXN=$((NA > NB ? NA : NB))
UNWIND=$((4 + 4 * MAXN))
LOG="$LOG_DIR/$HARNESS_FN-${NA}x${NB}.log"

# 注：不加 --unsigned-overflow-check —— 本库（及参考模型）有意使用
# `i-- > 0` 这类无符号回绕写法，C 语义良定义；有符号溢出仍检查。
# shellcheck disable=SC2086
if "$CBMC" --function "$HARNESS_FN" \
        --bounds-check --pointer-check --signed-overflow-check \
        --undefined-shift-check --unwinding-assertions \
        --unwind "$UNWIND" \
        --havoc-undefined-functions -DIN_LIMBS="$NA" -DIN_LIMBS_B="$NB" $EXTRA -I. \
        $SRC "$HARNESS" > "$LOG" 2>&1; then
    echo "PASS: $HARNESS_FN ${NA}x${NB}"
else
    echo "FAIL: $HARNESS_FN ${NA}x${NB}（日志：$LOG）"
    tail -15 "$LOG"
    exit 1
fi
