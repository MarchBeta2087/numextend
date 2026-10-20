#!/usr/bin/env python3
"""文档引用检查（CI）：校验 Markdown 中引用的仓库路径与 CMake preset 名。

只检查反引号里的“像仓库路径”的 token，避免把正文/URL 当路径：
  - nex/ scripts/ bench/ docs/ contrib/ tests/ verify/ cmake/ demo/ examples/
  - 根目录文件 README.md / TODO.md / CHANGELOG.md / CMakePresets.json ...
跳过生成物（build*、_install*）与通配符/占位符（* < > { } $ ... ~ |）。
另有白名单 scripts/docs_check_allow.txt（每行一个 token/前缀），用于示例路径。

用法：python3 scripts/check-docs.py [-v]
退出码：0 无缺失；1 有缺失/非法 preset。
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOW_FILE = ROOT / "scripts" / "docs_check_allow.txt"

ROOT_FILES = {
    "README.md", "TODO.md", "CHANGELOG.md", "LICENSE", "CMakeLists.txt",
    "CMakePresets.json", "coding_standard.md", "SECURITY.md",
    "CODE_OF_CONDUCT.md", ".markdownlint.yml",
}
DIR_PREFIXES = ("nex/", "scripts/", "bench/", "docs/", "contrib/", "tests/",
                "verify/", "cmake/", "demo/", "examples/")
GENERATED_PREFIXES = ("build/", "build-", "_install", "docs/manual/api-")

TOKEN_RE = re.compile(r"^(?:\./)?([A-Za-z0-9_][A-Za-z0-9_./+-]*)$")
PRESET_RE = re.compile(r"--preset\s+([A-Za-z0-9_-]+)")


def doc_files() -> list[Path]:
    files = [ROOT / "README.md", ROOT / "TODO.md"]
    files += sorted((ROOT / "docs").glob("*.md"))
    files += sorted((ROOT / "contrib").glob("*/README.md"))
    files += [ROOT / "bench" / "baseline" / "README.md",
              ROOT / "tests" / "fuzz" / "README.md",
              ROOT / "verify" / "cbmc" / "README.md"]
    return [p for p in files if p.is_file()]


def load_allow() -> list[str]:
    if not ALLOW_FILE.is_file():
        return []
    out = []
    for line in ALLOW_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.append(line)
    return out


def presets() -> set[str]:
    try:
        data = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return set()
    return {p["name"] for p in data.get("configurePresets", [])}


def exists(path_token: str) -> bool:
    """路径存在；也接受“模块名/可执行名”写法（token + .c/.h/.in）。"""
    if (ROOT / path_token).exists():
        return True
    if path_token.endswith(".h") and (ROOT / (path_token + ".in")).exists():
        return True
    if list(ROOT.glob(path_token + ".*")):
        return True
    return False


def skip(token: str) -> bool:
    if any(ch in token for ch in "*<>{}~|$"):
        return True
    if "..." in token or token.startswith("http"):
        return True
    if token.startswith(GENERATED_PREFIXES):
        return True
    return False


def main(argv: list[str]) -> int:
    verbose = "-v" in argv
    allow = load_allow()
    valid_presets = presets()
    missing: list[str] = []
    bad_presets: list[str] = []
    checked = 0

    for path in doc_files():
        rel = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            # 路径：只看反引号里的 token
            for span in re.findall(r"`([^`\n]+)`", line):
                for raw in re.findall(r"[^\s`'\"]+", span):
                    raw = raw.split("::", 1)[0]          # tests/x::case
                    tok = raw.rstrip(".,:;)]}").lstrip("(")
                    if not tok or skip(tok):
                        continue
                    if tok in ROOT_FILES:
                        checked += 1
                        if not exists(tok):
                            missing.append(f"{rel}:{lineno}: {tok}")
                        continue
                    if not tok.startswith(DIR_PREFIXES):
                        continue
                    checked += 1
                    if any(tok == a or tok.startswith(a) for a in allow):
                        continue
                    if not exists(tok):
                        missing.append(f"{rel}:{lineno}: {tok}")
            # preset 名：反引号或命令行
            if "cmake" not in line.lower():
                continue
            for name in PRESET_RE.findall(line):
                if name.startswith("<") or not valid_presets:
                    continue
                if name not in valid_presets:
                    bad_presets.append(f"{rel}:{lineno}: --preset {name}")

    if verbose:
        print(f"checked {checked} path tokens in {len(doc_files())} files")
    for item in missing:
        print("MISSING:", item)
    for item in bad_presets:
        print("BAD PRESET:", item)
    if missing or bad_presets:
        print(f"FAIL: {len(missing)} missing path(s), {len(bad_presets)} bad preset(s)")
        return 1
    print(f"OK: docs references valid（{checked} 个路径 token）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
