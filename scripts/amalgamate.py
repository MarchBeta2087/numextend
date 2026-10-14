#!/usr/bin/env python3
"""单文件组合（amalgamation）生成器（TODO §B4）。

用法：
    python3 scripts/amalgamate.py --out DIR [--modules all|a,b]

生成 DIR/nex_amalgam.h 与 DIR/nex_amalgam.c：

  - 依赖闭包来自 cmake/nex_modules.txt（复用 scripts/modules.py）；
  - nex_amalgam.h：闭包内公开头（保留 include guard 与 extern "C"）；
  - nex_amalgam.c：#include "nex_amalgam.h" + 闭包内内部头 + 各源文件；
  - 剥离所有 `#include "nex/..."`（保留系统 `<...>` 头）；
  - 跨文件重名的 `static` 符号按文件前缀改名（单 TU 下避免重复定义）。

生成物是纯 C99，可直接：
    cc -c nex_amalgam.c -I. -o nex_amalgam.o
或经 CMake `-DNEX_AMALGAM=ON` 构建并跑全量测试。
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import modules as mod  # noqa: E402

NEX_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"nex/[^"]+"')
# 行首 static 声明中的标识符（函数名 / 变量名）：后随 ( [ ; =
STATIC_DEF_RE = re.compile(
    r'^static\b[^;{(]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*[\(\[;=]', re.M)


def read(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def strip_nex_includes(text):
    return "\n".join(line for line in text.splitlines()
                     if not NEX_INCLUDE_RE.match(line))


def file_tag(path):
    rel = os.path.relpath(path, ROOT)
    return re.sub(r"[^A-Za-z0-9]+", "_", rel).strip("_")


def collect_static_names(path):
    return set(STATIC_DEF_RE.findall(read(path)))


def build_static_renames(sources):
    """跨文件重名的 static 名 → 每文件改名映射。"""
    per_file = {src: collect_static_names(src) for src in sources}
    count = {}
    for names in per_file.values():
        for name in names:
            count[name] = count.get(name, 0) + 1
    colliding = {name for name, n in count.items() if n > 1}
    renames = {}
    for src, names in per_file.items():
        hit = names & colliding
        if hit:
            tag = file_tag(src)
            renames[src] = {name: "%s_%s" % (tag, name) for name in hit}
    return renames, colliding


def apply_renames(text, mapping):
    if not mapping:
        return text
    pattern = re.compile(r"(?<![\w.>])(" +
                         "|".join(re.escape(n) for n in mapping) + r")\b")
    return pattern.sub(lambda m: mapping[m.group(1)], text)


def gather_internal_headers(sources):
    seen = []
    for src in sources:
        for line in read(src).splitlines():
            m = re.match(r'\s*#\s*include\s*"(nex/[^"]+_internal\.h)"', line)
            if m and m.group(1) not in seen:
                seen.append(m.group(1))
    return seen


def version_block():
    text = read(os.path.join(ROOT, "CMakeLists.txt"))
    m = re.search(r"project\(\s*numextend\s+VERSION\s+([0-9]+)\.([0-9]+)\.([0-9]+)",
                  text)
    if not m:
        raise SystemExit("无法从 CMakeLists.txt 解析 project(VERSION)")
    major, minor, patch = m.group(1), m.group(2), m.group(3)
    return "\n".join([
        "#ifndef NEX_VERSION_H",
        "#define NEX_VERSION_H",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "#define NEX_VERSION_MAJOR %s" % major,
        "#define NEX_VERSION_MINOR %s" % minor,
        "#define NEX_VERSION_PATCH %s" % patch,
        '#define NEX_VERSION_STRING "%s.%s.%s"' % (major, minor, patch),
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* NEX_VERSION_H */",
    ])


def emit_header(public_headers):
    parts = []
    parts.append("/*")
    parts.append(" * nex_amalgam.h —— Numextend 单文件组合头（自动生成，勿手改）。")
    parts.append(" * 生成器：scripts/amalgamate.py（TODO §B4）。")
    parts.append(" */")
    parts.append("#ifndef NEX_AMALGAM_H")
    parts.append("#define NEX_AMALGAM_H")
    parts.append("")
    parts.append(version_block())
    parts.append("")
    for hdr in public_headers:
        parts.append("/* ===== %s ===== */" % hdr)
        parts.append(strip_nex_includes(read(hdr)).strip())
        parts.append("")
    parts.append("#endif /* NEX_AMALGAM_H */")
    parts.append("")
    return "\n".join(parts)


def emit_source(internal_headers, sources, renames):
    parts = []
    parts.append("/*")
    parts.append(" * nex_amalgam.c —— Numextend 单文件组合实现（自动生成，勿手改）。")
    parts.append(" * 生成器：scripts/amalgamate.py（TODO §B4）。")
    parts.append(" */")
    parts.append('#include "nex_amalgam.h"')
    parts.append("")
    for hdr in internal_headers:
        parts.append("/* ===== %s ===== */" % hdr)
        parts.append(strip_nex_includes(read(hdr)).strip())
        parts.append("")
    for src in sources:
        text = strip_nex_includes(read(src))
        text = apply_renames(text, renames.get(src, {}))
        parts.append("/* ===== %s ===== */" % os.path.relpath(src, ROOT))
        parts.append(text.strip())
        parts.append("")
    return "\n".join(parts)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="输出目录")
    ap.add_argument("--modules", default="all",
                    help="all 或逗号分隔模块名（默认 all）")
    args = ap.parse_args(argv[1:])

    modules, order = mod.parse()
    if args.modules == "all":
        requested = order
    else:
        requested = [m.strip() for m in args.modules.split(",") if m.strip()]
    names = mod.closure(modules, requested)
    seen = set(names)
    sources = []
    public_headers = []
    for name in order:
        if name in seen:
            sources.extend(modules[name]["sources"])
            public_headers.extend(modules[name]["headers"])

    renames, colliding = build_static_renames(sources)
    internal_headers = gather_internal_headers(sources)

    out_h = os.path.join(args.out, "nex_amalgam.h")
    out_c = os.path.join(args.out, "nex_amalgam.c")
    write(out_h, emit_header(public_headers))
    write(out_c, emit_source(internal_headers, sources, renames))

    print("amalgam: %d modules, %d sources, %d public headers"
          % (len(names), len(sources), len(public_headers)))
    print("static renames: %d files, %d colliding names"
          % (len(renames), len(colliding)))
    print("  header: %s" % out_h)
    print("  source: %s" % out_c)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
