#!/usr/bin/env python3
"""模块清单工具（TODO §B1/B2）。

读取 cmake/nex_modules.txt（子集构建与文档生成的单一来源），支持：

    check                    校验清单与源码树一致（dep 已知、每个 .c 与公开头
                             恰好登记一次），CI 用；
    list                     列出全部模块及其依赖；
    closure <mod> [<mod>...] 按依赖闭包展开，打印模块 / 源文件 / 公开头；
    gen-docs [out]           生成模块参考 Markdown（默认 docs/modules.md）；
    docs <mod> [<mod>...]    打印子集的保留/移除清单（模块 / 文件 / 函数）。

清单字段：<module>|<deps>|<sources>|<public_headers>，字段内以 ',' 分隔，
'#' 开头为注释。
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "cmake", "nex_modules.txt")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import api_symbols  # noqa: E402  （复用公开函数名识别）


def parse():
    modules = {}
    order = []
    with open(MANIFEST, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) < 4:
                raise SystemExit("manifest: 字段不足（需 4 个）：" + line)
            name = parts[0].strip()
            deps = [x.strip() for x in parts[1].split(",") if x.strip()]
            srcs = [x.strip() for x in parts[2].split(",") if x.strip()]
            hdrs = [x.strip() for x in parts[3].split(",") if x.strip()]
            modules[name] = {"deps": deps, "sources": srcs, "headers": hdrs}
            order.append(name)
    return modules, order


def closure(modules, requested):
    seen = []
    queue = list(requested)
    while queue:
        name = queue.pop(0)
        if name in seen:
            continue
        if name not in modules:
            raise SystemExit("未知模块 '%s'；可用：%s" %
                             (name, ", ".join(modules)))
        seen.append(name)
        queue.extend(modules[name]["deps"])
    return seen


def _rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def functions_in_header(path):
    """公开头中声明的公开函数名（剔除注释）。"""
    text = open(path, encoding="utf-8", errors="ignore").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return set(m.group(1) for m in api_symbols.NAME_RE.finditer(text))


def module_functions(modules, name):
    funcs = set()
    for hdr in modules[name]["headers"]:
        path = os.path.join(ROOT, hdr)
        if os.path.exists(path):
            funcs |= functions_in_header(path)
    return funcs


def cmd_gen_docs(modules, order, out_path):
    lines = []
    lines.append("# 模块参考（自动生成）")
    lines.append("")
    lines.append("> 由 `scripts/modules.py gen-docs` 生成（TODO §B5），数据源为")
    lines.append("> `cmake/nex_modules.txt` 与公开头文件。请勿手改；")
    lines.append("> 运行 `sh scripts/docs.sh` 重新生成。")
    lines.append("")
    lines.append("## 1. 子集构建与依赖闭包")
    lines.append("")
    lines.append("构建选项 `NEX_MODULES`（默认 `all`）接受模块名列表，CMake 按")
    lines.append("**依赖闭包**自动补齐：只编译闭包内源文件、只安装闭包内公开头。")
    lines.append("依赖允许成环（`bigint_bin ↔ bigint_conv`——bin 的十进制字符串")
    lines.append("快速路径调用 conv）。查看某子集：")
    lines.append("`python scripts/modules.py closure <mod...>`；某子集的保留/移除")
    lines.append("清单：`python scripts/modules.py docs <mod...>`。")
    lines.append("")
    lines.append("## 2. 模块与依赖")
    lines.append("")
    for name in order:
        info = modules[name]
        deps = ", ".join("`%s`" % d for d in info["deps"]) or "—"
        lines.append("### `%s`" % name)
        lines.append("")
        lines.append("- 直接依赖：%s" % deps)
        lines.append("- 源文件：%s" %
                     (", ".join("`%s`" % s for s in info["sources"]) or "（仅头）"))
        lines.append("- 公开头：%s" %
                     (", ".join("`%s`" % h for h in info["headers"]) or "—"))
        funcs = sorted(module_functions(modules, name))
        lines.append("- 公开函数（%d）：" % len(funcs))
        for func in funcs:
            lines.append("  - `%s`" % func)
        lines.append("")
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("已生成 %s" % _rel(out_path))
    return 0


def cmd_docs(modules, order, requested):
    """打印子集的保留/移除清单（模块 / 文件 / 函数）。"""
    names = set(closure(modules, requested))
    print("# 子集清单：NEX_MODULES=%s" % ",".join(requested))
    print("")
    kept_m = [n for n in order if n in names]
    removed_m = [n for n in order if n not in names]
    print("## 模块")
    print("- 保留（%d）：%s" % (len(kept_m), ", ".join(kept_m)))
    print("- 移除（%d）：%s" %
          (len(removed_m), ", ".join(removed_m) or "—"))
    print("")
    kept_s = [s for n in kept_m for s in modules[n]["sources"]]
    removed_s = [s for n in removed_m for s in modules[n]["sources"]]
    kept_h = [h for n in kept_m for h in modules[n]["headers"]]
    removed_h = [h for n in removed_m for h in modules[n]["headers"]]
    print("## 文件")
    print("- 保留源文件（%d）：%s" % (len(kept_s), ", ".join(kept_s)))
    print("- 移除源文件（%d）：%s" %
          (len(removed_s), ", ".join(removed_s) or "—"))
    print("- 保留公开头（%d）：%s" % (len(kept_h), ", ".join(kept_h)))
    print("- 移除公开头（%d）：%s" %
          (len(removed_h), ", ".join(removed_h) or "—"))
    print("")
    all_funcs = set()
    kept_funcs = set()
    for name in order:
        funcs = module_functions(modules, name)
        all_funcs |= funcs
        if name in names:
            kept_funcs |= funcs
    removed_funcs = all_funcs - kept_funcs
    print("## 公开函数")
    print("- 保留（%d）：%s" %
          (len(kept_funcs), ", ".join(sorted(kept_funcs))))
    print("- 移除（%d）：%s" %
          (len(removed_funcs), ", ".join(sorted(removed_funcs)) or "—"))
    return 0


def cmd_check(modules, order):
    errors = []
    for name, info in modules.items():
        for dep in info["deps"]:
            if dep not in modules:
                errors.append("%s: 未知依赖 '%s'" % (name, dep))
    all_c = sorted(_rel(p) for p in
                   glob.glob(os.path.join(ROOT, "nex", "**", "*.c"),
                             recursive=True))
    listed_c = [s for name in order for s in modules[name]["sources"]]
    for src in all_c:
        count = listed_c.count(src)
        if count != 1:
            errors.append("源文件 %s 登记次数 = %d（应恰好 1）" % (src, count))
    for src in listed_c:
        if src not in all_c:
            errors.append("清单中的源文件不存在: " + src)
    all_h = sorted(_rel(p) for p in
                   glob.glob(os.path.join(ROOT, "nex", "**", "*.h"),
                             recursive=True)
                   if not p.endswith("_internal.h"))
    listed_h = [h for name in order for h in modules[name]["headers"]]
    for hdr in all_h:
        count = listed_h.count(hdr)
        if count != 1:
            errors.append("公开头 %s 登记次数 = %d（应恰好 1）" % (hdr, count))
    for hdr in listed_h:
        if hdr not in all_h:
            errors.append("清单中的头文件不存在: " + hdr)
    if errors:
        for err in errors:
            print("FAIL:", err)
        return 1
    print("OK: %d 模块、%d 源文件、%d 公开头，清单与源码树一致" %
          (len(modules), len(all_c), len(all_h)))
    return 0


def cmd_list(modules, order):
    for name in order:
        print("%-20s deps: %s" % (name, ", ".join(modules[name]["deps"]) or "-"))
    return 0


def cmd_closure(modules, order, requested):
    names = closure(modules, requested)
    seen = set(names)
    print("modules:")
    for name in names:
        print("  " + name)
    print("sources:")
    for name in order:
        if name in seen:
            for src in modules[name]["sources"]:
                print("  " + src)
    print("headers:")
    for name in order:
        if name in seen:
            for hdr in modules[name]["headers"]:
                print("  " + hdr)
    return 0


def main(argv):
    modules, order = parse()
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "check":
        return cmd_check(modules, order)
    if cmd == "list":
        return cmd_list(modules, order)
    if cmd == "closure":
        if len(argv) < 3:
            raise SystemExit("用法: modules.py closure <module> [<module>...]")
        return cmd_closure(modules, order, argv[2:])
    if cmd == "gen-docs":
        out = argv[2] if len(argv) > 2 else os.path.join(ROOT, "docs", "modules.md")
        return cmd_gen_docs(modules, order, out)
    if cmd == "docs":
        if len(argv) < 3:
            raise SystemExit("用法: modules.py docs <module> [<module>...]")
        return cmd_docs(modules, order, argv[2:])
    raise SystemExit("未知命令: " + cmd)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
