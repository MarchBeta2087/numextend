#!/bin/sh
# 生成参考手册：
#   1) 从公开头文件生成 API 章节 → docs/manual/api-*.md（scripts/gen_api_doc.py）；
#   2) 用 Pandoc 把 docs/manual/*.md 合成单文件 HTML → docs/manual.html。
# 用法：sh scripts/docs.sh
set -e
cd "$(dirname "$0")/.."

"${PYTHON:-python3}" scripts/gen_api_doc.py docs/manual

if command -v pandoc > /dev/null 2>&1; then
    pandoc $(ls docs/manual/*.md | sort) \
        --from gfm --to html5 --standalone --embed-resources \
        --toc --toc-depth=3 \
        --metadata title="Numextend 参考手册" \
        --css docs/manual.css \
        -o docs/manual.html
    echo "已生成 docs/manual.html"
else
    echo "未找到 pandoc：仅生成 Markdown（docs/manual/）"
fi
