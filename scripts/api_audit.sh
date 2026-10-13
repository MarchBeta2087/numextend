#!/bin/sh
# 公开 API 审计（CI 每次运行）：
#   1) 公开头文件自包含（可独立编译）且含 include guard；
#   2) 公开声明与库内定义一致（scripts/api_symbols.py）。
# 用法：sh scripts/api_audit.sh [build_dir]  （默认 build）
set -e
cd "$(dirname "$0")/.."
BUILD="${1:-build}"
CC="${CC:-cc}"
status=0

echo "== 公开头文件自包含 + include guard =="
mkdir -p build
for h in $(find nex -name '*.h' ! -name '*_internal.h' ! -name '*.h.in' | sort); do
    if ! head -1 "$h" | grep -q '#ifndef'; then
        echo "FAIL(no guard): $h"
        status=1
    fi
    printf '#include "%s"\nint main(void){return 0;}\n' "$h" > build/_hdr.c
    if ! "$CC" -std=c99 -Wall -Wextra -Wpedantic -I. -I"$BUILD/include" \
            -fsyntax-only build/_hdr.c; then
        echo "FAIL(not self-contained): $h"
        status=1
    fi
done
rm -f build/_hdr.c
[ "$status" = 0 ] && echo "OK: all public headers self-contained with guards"

echo
echo "== 声明 / 定义一致性 =="
"${PYTHON:-python3}" scripts/api_symbols.py "$BUILD" || status=1
echo
echo "audit status: $status"
exit $status
