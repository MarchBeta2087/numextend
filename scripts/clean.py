#!/usr/bin/env python3
"""构建/产物清理工具（P2 构建治理）。

默认只列出候选与大小（dry-run）；删除必须显式 --yes。
只处理仓库根内的目录/文件，不跟随符号链接，拒绝删除仓库根本身。

用法：
  python3 scripts/clean.py                          # 列出全部候选
  python3 scripts/clean.py --builds                 # 只列构建目录
  python3 scripts/clean.py --builds --legacy        # 仅根目录 build-* 历史目录
  python3 scripts/clean.py --builds --presets       # 仅 build/<preset>（CMakePresets）
  python3 scripts/clean.py --builds --stale         # CMakeCache 源目录不匹配的
  python3 scripts/clean.py --builds --keep 'build/opencl' --yes
  python3 scripts/clean.py --installs --coverage --artifacts --pycache --yes
  python3 scripts/clean.py --json

退出码：0 正常（含 dry-run）；1 有拒绝/删除失败。
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import stat
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def human(n: int) -> str:
    v = float(n)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if v < 1024.0 or unit == "TB":
            return f"{v:.1f}{unit}" if unit != "B" else f"{int(v)}B"
        v /= 1024.0
    return f"{v:.1f}TB"


def dir_size(path: Path) -> int:
    total = 0
    stack = [path]
    while stack:
        cur = stack.pop()
        try:
            with os.scandir(cur) as it:
                for e in it:
                    try:
                        if e.is_symlink():
                            continue
                        if e.is_dir(follow_symlinks=False):
                            stack.append(Path(e.path))
                        elif e.is_file(follow_symlinks=False):
                            total += e.stat(follow_symlinks=False).st_size
                    except OSError:
                        pass
        except OSError:
            pass
    return total


def cache_source(build_dir: Path) -> str | None:
    cache = build_dir / "CMakeCache.txt"
    try:
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_HOME_DIRECTORY:"):
                return line.split("=", 1)[1].strip()
    except OSError:
        return None
    return None


def normalize_source(src: str) -> str:
    """把 WSL 的 /mnt/c/... 归一成 C:/...，避免跨环境误判。"""
    m = re.match(r"^/mnt/([A-Za-z])/(.*)$", src)
    if m:
        return f"{m.group(1).upper()}:/{m.group(2)}"
    return src


def is_stale(build_dir: Path) -> bool:
    src = cache_source(build_dir)
    if not src:
        return False
    try:
        return os.path.normcase(os.path.realpath(normalize_source(src))) !=             os.path.normcase(os.path.realpath(ROOT))
    except OSError:
        return True


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def safe_targets(paths: list[Path]) -> tuple[list[Path], list[tuple[Path, str]]]:
    """过滤：必须位于仓库内、非根、非符号链接。"""
    ok: list[Path] = []
    refused: list[tuple[Path, str]] = []
    root_real = os.path.realpath(ROOT)
    for p in paths:
        try:
            real = os.path.realpath(p)
        except OSError as exc:
            refused.append((p, f"resolve 失败：{exc}"))
            continue
        if os.path.islink(p):
            refused.append((p, "符号链接，跳过"))
            continue
        if os.path.normcase(real) == os.path.normcase(root_real):
            refused.append((p, "拒绝删除仓库根"))
            continue
        if not os.path.normcase(real).startswith(os.path.normcase(root_real) + os.sep):
            refused.append((p, "不在仓库根内"))
            continue
        ok.append(p)
    return ok, refused


def remove(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path, onerror=_on_error)
    elif path.exists() or path.is_symlink():
        try:
            os.chmod(path, stat.S_IWRITE)
        except OSError:
            pass
        path.unlink(missing_ok=True)


def _on_error(func, p, exc_info):  # Windows 只读文件
    try:
        os.chmod(p, stat.S_IWRITE)
        func(p)
    except OSError:
        raise


def collect(args) -> list[tuple[Path, str]]:
    items: list[tuple[Path, str]] = []
    want_builds = args.builds or args.legacy or args.presets
    legacy = args.legacy or (args.builds and not args.presets)
    presets = args.presets or (args.builds and not args.legacy)

    if want_builds:
        if legacy:
            for p in sorted(ROOT.glob("build*")):
                if p.is_dir() and not p.is_symlink():
                    if p.name == "build" and presets:
                        continue
                    items.append((p, "build(legacy)"))
        if presets:
            presets_root = ROOT / "build"
            if presets_root.is_dir() and not presets_root.is_symlink():
                for p in sorted(presets_root.iterdir()):
                    if p.is_dir() and not p.is_symlink():
                        items.append((p, "build(preset)"))

    if args.installs:
        for p in sorted(ROOT.glob("_install*")):
            if p.is_dir() and not p.is_symlink():
                items.append((p, "install"))

    if args.coverage:
        for p in sorted(ROOT.glob("*.gcov")):
            if p.is_file():
                items.append((p, "coverage"))
        cov = ROOT / "coverage"
        if cov.is_dir() and not cov.is_symlink():
            items.append((cov, "coverage"))

    if args.artifacts:
        patterns = [
            "bench-*.json",
            "bench-*.txt",
            "bench-*.md",
            "bench-math-*.json",
            "bench-math-*.txt",
            "bench-math-*.md",
        ]
        for pat in patterns:
            for p in sorted(ROOT.glob(pat)):
                if p.is_file():
                    items.append((p, "artifact"))
        perf_ci = ROOT / "docs" / "performance-ci.md"
        if perf_ci.is_file():
            items.append((perf_ci, "artifact"))

    if args.pycache:
        for base in ("scripts", "tests", "bench", "tools", "demo", "examples"):
            d = ROOT / base
            if d.is_dir():
                for p in sorted(d.rglob("__pycache__")):
                    if p.is_dir() and not p.is_symlink():
                        items.append((p, "pycache"))

    # 去重（按解析路径）
    seen: set[str] = set()
    uniq: list[tuple[Path, str]] = []
    for p, why in items:
        key = os.path.normcase(os.path.realpath(p))
        if key in seen:
            continue
        seen.add(key)
        uniq.append((p, why))

    if args.stale:
        uniq = [(p, why) for p, why in uniq if is_stale(p)]

    if args.keep:
        kept = []
        for p, why in uniq:
            name = rel(p)
            if any(fnmatch.fnmatch(name, pat) or fnmatch.fnmatch(p.name, pat)
                   for pat in args.keep):
                continue
            kept.append((p, why))
        uniq = kept

    return uniq


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="清理仓库内的构建目录与生成产物（默认 dry-run）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("用法：", 1)[1] if "用法：" in __doc__ else None,
    )
    ap.add_argument("--builds", action="store_true", help="所有构建目录")
    ap.add_argument("--legacy", action="store_true", help="仅根目录 build-*（历史）")
    ap.add_argument("--presets", action="store_true", help="仅 build/<preset>（CMakePresets）")
    ap.add_argument("--installs", action="store_true", help="安装树 _install*")
    ap.add_argument("--coverage", action="store_true", help="覆盖率产物")
    ap.add_argument("--artifacts", action="store_true", help="benchmark 输出/报告")
    ap.add_argument("--pycache", action="store_true", help="__pycache__")
    ap.add_argument("--stale", action="store_true",
                    help="仅 CMakeCache 源目录与当前仓库不一致的构建目录")
    ap.add_argument("--keep", action="append", default=[], metavar="GLOB",
                    help="保留匹配的相对路径/目录名（可重复）")
    ap.add_argument("--dry-run", action="store_true",
                    help="只列清单（默认行为；与 --yes 同用时以 dry-run 为准）")
    ap.add_argument("--yes", action="store_true", help="真正删除（默认只列清单）")
    ap.add_argument("--json", action="store_true", help="JSON 输出")
    args = ap.parse_args(argv)
    if args.dry_run:
        args.yes = False

    if not any([args.builds, args.legacy, args.presets, args.installs,
                args.coverage, args.artifacts, args.pycache]):
        args.builds = True

    items = collect(args)
    total = sum(dir_size(p) if p.is_dir() else p.stat().st_size for p, _ in items)
    ok, refused = safe_targets([p for p, _ in items])
    ok_set = {os.path.normcase(os.path.realpath(p)) for p in ok}
    items = [(p, why) for p, why in items
             if os.path.normcase(os.path.realpath(p)) in ok_set]

    if args.json:
        print(json.dumps({
            "repo": str(ROOT),
            "dry_run": not args.yes,
            "total_bytes": total,
            "items": [{"path": rel(p), "kind": why,
                       "bytes": dir_size(p) if p.is_dir() else p.stat().st_size}
                      for p, why in items],
            "refused": [{"path": rel(p), "reason": why} for p, why in refused],
        }, ensure_ascii=False, indent=2))
    else:
        mode = "DRY-RUN（加 --yes 执行删除）" if not args.yes else "DELETE"
        print(f"仓库：{ROOT}")
        print(f"模式：{mode}")
        for p, why in items:
            size = dir_size(p) if p.is_dir() else p.stat().st_size
            print(f"  {human(size):>9}  {why:<14} {rel(p)}")
        for p, why in refused:
            print(f"  {'refused':>9}  {why:<14} {rel(p)}")
        print(f"合计：{human(total)}（{len(items)} 项）")

    if not args.yes:
        return 0

    failed = 0
    for p, _ in items:
        try:
            remove(p)
        except OSError as exc:
            failed += 1
            print(f"删除失败：{rel(p)}：{exc}", file=sys.stderr)

    # 清理空的 build/ 根（若按 preset 删空且本身不含内容）
    presets_root = ROOT / "build"
    if presets_root.is_dir():
        try:
            if not any(presets_root.iterdir()):
                presets_root.rmdir()
        except OSError:
            pass

    if not args.json:
        print(f"已删除 {len(items) - failed}/{len(items)} 项，回收 {human(total)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
