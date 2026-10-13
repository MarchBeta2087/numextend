#!/usr/bin/env python3
"""公开 API 符号审计：比对公开头文件声明与库中定义/导出的全局符号。

用法：python3 scripts/api_symbols.py [build_dir]   （默认 build）
返回码：0 通过；1 存在「已声明但未定义」的公开函数。
"""
import os
import re
import sys
import glob
import subprocess

# 允许的公开模块前缀（公开函数须以此开头）
PREFIXES = (
    "bigint_bin", "bigint_dec", "bigint_bin64", "bigint_conv",
    "bigfrac", "bigfloat", "bigdecimal",
    "bigcomplex_float", "bigcomplex_decimal",
    "nex", "fft", "ntt",
)
NAME_RE = re.compile(r"\b((?:" + "|".join(PREFIXES) + r")_[A-Za-z0-9_]+)\s*\(")


def public_declarations():
    """收集公开头文件中出现的函数名（剔除注释）。"""
    decl = set()
    for h in glob.glob("nex/**/*.h", recursive=True):
        if h.endswith("_internal.h") or h.endswith(".h.in"):
            continue
        text = open(h, encoding="utf-8", errors="ignore").read()
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        for match in NAME_RE.finditer(text):
            decl.add(match.group(1))
    return decl


def defined_symbols(build_dir):
    """读取静态库中定义的文本段全局符号。"""
    lib = os.path.join(build_dir, "libnex.a")
    out = subprocess.run(["nm", "-g", "--defined-only", lib],
                         capture_output=True, text=True, check=True).stdout
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-2] in ("T", "W"):
            syms.add(parts[-1])
    return syms


def main():
    build_dir = sys.argv[1] if len(sys.argv) > 1 else "build"
    decl = public_declarations()
    defined = defined_symbols(build_dir)

    missing = sorted(d for d in decl if d not in defined)
    undecl = sorted(s for s in defined if s not in decl and s.startswith("nex_"))

    print("public declarations: %d; defined text symbols: %d"
          % (len(decl), len(defined)))
    if missing:
        print("[FAIL] declared but not defined (%d):" % len(missing))
        for name in missing:
            print("   ", name)
    else:
        print("OK: every public declaration has a definition")
    if undecl:
        print("[info] internal symbols exported (%d): %s"
              % (len(undecl), ", ".join(undecl)))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
