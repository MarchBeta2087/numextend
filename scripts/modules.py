#!/usr/bin/env python3
"""模块清单工具（TODO §B1/B2）。

读取 cmake/nex_modules.txt（子集构建与文档生成的单一来源），支持：

    check                    校验清单与源码树一致（dep 已知、每个 .c 与公开头
                             恰好登记一次），CI 用；
    list                     列出全部模块及其依赖；
    closure <mod> [<mod>...] 按依赖闭包展开，打印模块 / 源文件 / 公开头。

清单字段：<module>|<deps>|<sources>|<public_headers>，字段内以 ',' 分隔，
'#' 开头为注释。
"""

import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "cmake", "nex_modules.txt")


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
    raise SystemExit("未知命令: " + cmd)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
