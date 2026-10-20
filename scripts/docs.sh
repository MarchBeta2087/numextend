#!/bin/sh
# 生成参考手册：
#   1) 从公开头文件生成 API 章节 → docs/manual/api-*.md（scripts/gen_api_doc.py）；
#   2) 用 Pandoc 把 docs/manual/*.md 合成单文件 HTML → docs/manual.html。
# 用法：sh scripts/docs.sh
set -e
cd "$(dirname "$0")/.."

"${PYTHON:-python3}" scripts/gen_api_doc.py docs/manual
"${PYTHON:-python3}" scripts/modules.py gen-docs docs/modules.md
"${PYTHON:-python3}" scripts/bench.py --list --markdown > docs/benchmark-suites.md
"${PYTHON:-python3}" scripts/modules.py gen-contrib-docs docs/contrib-modules.md

PANDOC="${PANDOC:-pandoc}"
if command -v "$PANDOC" > /dev/null 2>&1; then
    # pandoc 的行尾/CSS 解析行为不一致：CRLF 会被压成一行，非 .css 后缀
    # 又不会走 CSS 规范化。统一用 LF 且保留 .css 后缀的临时副本，保证
    # Windows 本地与 CI 输出一致。
    TMP_CSS="docs/.manual-lf.css"
    tr -d '\r' < docs/manual.css > "$TMP_CSS"
    "$PANDOC" $(ls docs/manual/*.md | sort) \
        --from gfm --to html5 --standalone --embed-resources \
        --toc --toc-depth=3 \
        --metadata title="Numextend 参考手册" \
        --css "$TMP_CSS" \
        -o docs/manual.html
    rm -f "$TMP_CSS"
    echo "已生成 docs/manual.html（$("$PANDOC" --version | head -1)）"
else
    echo "未找到 pandoc：仅生成 Markdown（docs/manual/）"
fi
