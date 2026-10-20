#!/usr/bin/env python3
"""统一性能测试入口（nex 模块 + contrib，任意组合，时间/内存预算）。

设计：
  - 清单 `bench/suites.json` 是唯一事实来源（suite → modules/contrib/preset/
    target/args/tier/预算）；模块名取自 `cmake/nex_modules.txt` 与 `contrib/`。
  - 档位：ci（默认 300 s）⊂ quick（1200 s，本地快速档）⊂ full（7200 s）；
    frozen 项仅在 --include-frozen 时参与。
  - 时间：全局 deadline + 单 suite timeout；超预算的 suite 标 skipped(budget)。
  - 内存：按单 suite 采样峰值 RSS，超限终止（100 ms 采样，软限制）；
    POSIX 另设 RLIMIT_AS = 2× 预算，避免先把机器拖死。
  - 构建：按需 `cmake --preset <p>` / `cmake --build build/<p> --target ...`；
    `--no-build` 使用现成产物。

用法：
  python3 scripts/bench.py --list
  python3 scripts/bench.py --modules bigint_bin,ntt --profile quick
  python3 scripts/bench.py --contrib nex_opencl,nex_parallel --profile ci
  python3 scripts/bench.py --all --include-frozen --max-time 3600 \\
      --json build/bench/results.json --markdown build/bench/report.md
  python3 scripts/bench.py --modules bigfloat --dry-run

退出码：0 成功（含预算内跳过 / optional 失败）；1 有非 optional 失败。
"""

from __future__ import annotations

import argparse
import ctypes
import datetime
import fnmatch
import html
import platform
import json
import os
import shlex
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITES = ROOT / "bench" / "suites.json"
PROFILES = {"ci": 300, "quick": 1200, "full": 7200}
TIERS = {"ci": {"ci"}, "quick": {"ci", "quick"}, "full": {"ci", "quick", "full"}}


def known_modules() -> set[str]:
    mods: set[str] = set()
    try:
        for line in (ROOT / "cmake" / "nex_modules.txt").read_text(
                encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                mods.add(line.split("|", 1)[0])
    except OSError:
        pass
    manifest = ROOT / "contrib" / "modules.txt"
    try:
        for line in manifest.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                mods.add(line.split("|", 1)[0].strip())
    except OSError:
        contrib = ROOT / "contrib"
        if contrib.is_dir():
            for p in contrib.iterdir():
                if p.is_dir() and p.name != "README.md":
                    mods.add(p.name)
    return mods


def load_suites() -> list[dict]:
    data = json.loads(SUITES.read_text(encoding="utf-8"))
    suites = data["suites"]
    for s in suites:
        s.setdefault("args", [])
        s.setdefault("modules", [])
        s.setdefault("contrib", [])
        s.setdefault("tier", "quick")
        s.setdefault("timeout_s", 600)
        s.setdefault("est_s", 60)
        s.setdefault("rss_mb", 0)
        s.setdefault("optional", False)
        s.setdefault("json", False)
    return suites


def select(suites: list[dict], args) -> list[dict]:
    mods = {m.strip() for m in (args.modules or "").split(",") if m.strip()}
    contribs = {c.strip() for c in (args.contrib or "").split(",") if c.strip()}
    if args.all:
        mods, contribs = set(), set()
    valid = known_modules()
    for name in mods | contribs:
        if name not in valid:
            sys.exit(f"未知模块/contrib：{name}\n可选：{' '.join(sorted(valid))}")

    tiers = set(TIERS[args.profile])
    if args.include_frozen:
        tiers.add("frozen")

    all_names = {s["name"] for s in suites}
    for name in args.suite:
        if name not in all_names:
            sys.exit(f"未知 suite：{name}")
    out = []
    for s in suites:
        if args.suite:
            if s["name"] not in args.suite:
                continue
            if s["tier"] == "frozen" and not args.include_frozen:
                continue
        elif s["tier"] not in tiers and not (
                args.include_frozen and s["tier"] == "frozen"):
            continue
        if mods and not (set(s["modules"]) & mods or set(s["contrib"]) & mods):
            continue
        if contribs and not (set(s["contrib"]) & contribs):
            continue
        if any(fnmatch.fnmatch(s["name"], pat) for pat in args.exclude):
            continue
        out.append(s)
    return out


def preset_dir(preset: str, build_root: Path) -> Path:
    return build_root / preset


def find_binary(preset: str, target: str, build_root: Path) -> Path | None:
    base = preset_dir(preset, build_root)
    names = [target, target + ".exe"]
    for name in names:
        p = base / name
        if p.is_file():
            return p
    for name in names:
        hits = list(base.rglob(name))
        if hits:
            return sorted(hits, key=lambda x: len(x.parts))[0]
    return None


def build_suites(suites: list[dict], build_root: Path) -> dict[str, str]:
    """按 preset 分组构建；返回失败的 preset → 原因（不中断整个 run）。"""
    by_preset: dict[str, set[str]] = {}
    for s in suites:
        by_preset.setdefault(s["preset"], set()).add(s["target"])
    errors: dict[str, str] = {}
    for preset, targets in by_preset.items():
        d = preset_dir(preset, build_root)
        if not (d / "CMakeCache.txt").is_file():
            print(f"== configure preset {preset} -> {d}")
            rc = subprocess.run(["cmake", "--preset", preset], cwd=ROOT).returncode
            if rc != 0:
                errors[preset] = "configure 失败（需要 CMake ≥3.19 + Ninja）"
                continue
        print(f"== build {preset}: {' '.join(sorted(targets))}")
        rc = subprocess.run(
            ["cmake", "--build", str(d), "--target", *sorted(targets),
             "--parallel"], cwd=ROOT).returncode
        if rc != 0:
            errors[preset] = "构建失败"
    return errors


# --------------------------- RSS 采样 --------------------------- #

class _PMC(ctypes.Structure):
    _fields_ = [
        ("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def peak_rss_kb(pid: int) -> int:
    if sys.platform.startswith("linux"):
        try:
            with open(f"/proc/{pid}/status", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("VmHWM:"):
                        return int(line.split()[1])
        except OSError:
            return 0
    elif sys.platform == "darwin":
        out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                             capture_output=True, text=True)
        try:
            return int(out.stdout.strip()) if out.stdout.strip() else 0
        except ValueError:
            return 0
    elif os.name == "nt":
        try:
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
            psapi = ctypes.WinDLL("psapi", use_last_error=True)
            access = 0x0400 | 0x0010  # QUERY_INFORMATION | VM_READ
            h = k32.OpenProcess(access, False, pid)
            if not h:
                access = 0x1000  # QUERY_LIMITED_INFORMATION
                h = k32.OpenProcess(access, False, pid)
            if not h:
                return 0
            pmc = _PMC()
            pmc.cb = ctypes.sizeof(_PMC)
            ok = psapi.GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb)
            k32.CloseHandle(h)
            return int(pmc.PeakWorkingSetSize // 1024) if ok else 0
        except OSError:
            return 0
    return 0


class RssWatch(threading.Thread):
    def __init__(self, proc: subprocess.Popen, limit_kb: int):
        super().__init__(daemon=True)
        self.proc = proc
        self.limit_kb = limit_kb
        self.peak_kb = 0
        self.exceeded = False
        self._stop = threading.Event()

    def run(self) -> None:
        while not self._stop.is_set():
            kb = peak_rss_kb(self.proc.pid)
            if kb > self.peak_kb:
                self.peak_kb = kb
            if self.limit_kb and kb > self.limit_kb:
                self.exceeded = True
                try:
                    self.proc.kill()
                except OSError:
                    pass
                return
            time.sleep(0.1)

    def stop(self) -> None:
        self._stop.set()


# --------------------------- 执行 --------------------------- #

def child_as_limit(limit_kb: int):
    """POSIX 下的地址空间硬上限（2× 预算），防止先把机器拖死。"""
    def _fn():
        try:
            import resource
            if limit_kb:
                lim = int(limit_kb) * 2 * 1024
                resource.setrlimit(resource.RLIMIT_AS, (lim, lim))
        except (ImportError, ValueError, OSError):
            pass
    return _fn


def run_suite(suite: dict, args, build_root: Path, out_dir: Path,
              remaining: float) -> dict:
    preset = suite["preset"]
    binpath = find_binary(preset, suite["target"], build_root)
    rec = {"suite": suite["name"], "tier": suite["tier"], "status": "fail",
           "seconds": 0.0, "peak_rss_mb": 0, "rc": None, "log": None,
           "json": None, "reason": ""}
    if binpath is None:
        rec["reason"] = f"找不到产物 {preset}/{suite['target']}（先构建或去掉 --no-build）"
        if suite["optional"]:
            rec["status"] = "skip(optional)"
        return rec

    out_dir.mkdir(parents=True, exist_ok=True)
    log = out_dir / f"{suite['name']}.log"
    jout = out_dir / f"{suite['name']}.json"
    argv = [str(binpath)] + [
        a.replace("${json}", str(jout)).replace("${outdir}", str(out_dir))
        for a in suite["args"]
    ]

    limit_kb = (suite["rss_mb"] or 0) * 1024
    if args.max_rss_mb:
        glob = args.max_rss_mb * 1024
        limit_kb = min(limit_kb, glob) if limit_kb else glob
    timeout = max(1.0, min(float(suite["timeout_s"]), remaining))

    t0 = time.monotonic()
    with open(log, "wb") as f:
        preexec = child_as_limit(limit_kb) if (os.name != "nt" and limit_kb) else None
        proc = subprocess.Popen(argv, cwd=ROOT, stdout=f,
                                stderr=subprocess.STDOUT, preexec_fn=preexec)
        watch = RssWatch(proc, limit_kb)
        watch.start()
        try:
            rc = proc.wait(timeout=timeout)
            status = "ok" if rc == 0 else "fail"
            reason = "" if rc == 0 else f"rc={rc}"
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            rc = None
            status = "timeout"
            reason = f">{timeout:.0f}s"
        finally:
            watch.stop()
            watch.join()
    rec.update({
        "status": status, "rc": rc, "log": str(log),
        "seconds": round(time.monotonic() - t0, 2),
        "peak_rss_mb": round(watch.peak_kb / 1024.0, 1),
        "json": str(jout) if suite["json"] and jout.is_file() else None,
        "reason": reason,
    })
    if watch.exceeded:
        rec["status"] = "rss"
        rec["reason"] = f"峰值 > {limit_kb // 1024} MB"
    if rec["status"] != "ok" and suite["optional"]:
        rec["status"] = "skip(optional)"
        rec["reason"] = rec["reason"] or "optional"
    return rec


def print_table(rows: list[dict], markdown: bool = False) -> None:
    if markdown:
        print("| suite | tier | 状态 | 耗时 s | 峰值 MB |")
        print("|---|---|---|---:|---:|")
        for r in rows:
            print(f"| `{r['suite']}` | {r['tier']} | {r['status']} | "
                  f"{r['seconds']:.1f} | {r['peak_rss_mb']:.0f} |")
        return
    for r in rows:
        print(f"  {r['status']:<14} {r['seconds']:>8.1f}s "
              f"{r['peak_rss_mb']:>7.0f}MB  {r['suite']}"
              + (f"  ({r['reason']})" if r["reason"] else ""))


def list_table(suites: list[dict], markdown: bool) -> None:
    if markdown:
        print("# 基准 suite 清单（自动生成）")
        print()
        print("> 由 `python3 scripts/bench.py --list --markdown` 生成，勿手改；"
              "新增/修改 suite 请编辑 `bench/suites.json`。")
        print()
        print("| suite | modules | contrib | tier | 预估 s | timeout s | RSS MB |")
        print("|---|---|---|---|---:|---:|---:|")
        for s in suites:
            print(f"| `{s['name']}` | {', '.join(s['modules'])} | "
                  f"{', '.join(s['contrib']) or '—'} | {s['tier']} | "
                  f"{s['est_s']} | {s['timeout_s']} | {s['rss_mb'] or '—'} |")
        return
    print(f"{'suite':<28} {'tier':<7} {'est':>6} {'timeout':>8} {'rss':>7}  modules / contrib")
    for s in suites:
        tags = ",".join(s["modules"]) + (" | " + ",".join(s["contrib"])
                                         if s["contrib"] else "")
        print(f"{s['name']:<28} {s['tier']:<7} {s['est_s']:>6} "
              f"{s['timeout_s']:>8} {s['rss_mb'] or '—':>7}  {tags}")


def render_html(rows: list[dict], profile: str, budget: float,
                elapsed: float) -> str:
    ok = sum(r["status"] == "ok" for r in rows)
    skipped = sum(r["status"].startswith("skip") for r in rows)
    failed = len(rows) - ok - skipped
    peak = max((r["peak_rss_mb"] for r in rows), default=0.0)
    commit = "unknown"
    try:
        commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                                cwd=ROOT, capture_output=True, text=True,
                                errors="replace").stdout.strip() or "unknown"
    except OSError:
        pass
    trs = []
    for r in rows:
        cls = {"ok": "ok"}.get(r["status"], "bad" if r["status"] in
                               ("fail", "timeout", "rss") else "skip")
        trs.append(
            f'<tr><td><span class="badge {cls}">{html.escape(r["status"])}</span></td>'
            f'<td class="name">{html.escape(r["suite"])}</td>'
            f'<td>{html.escape(r["tier"])}</td>'
            f'<td class="num">{r["seconds"]:.1f}</td>'
            f'<td class="num">{r["peak_rss_mb"]:.0f}</td>'
            f'<td>{html.escape(r["reason"] or "")}'
            f'{("<br><code>" + html.escape(r["log"]) + "</code>") if r["log"] else ""}</td></tr>')
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>性能测试报告 · {html.escape(commit)}</title>
<style>
body {{ margin: 0 auto; padding: 2rem 1.25rem 4rem; max-width: 72rem;
  font-family: -apple-system, "Segoe UI", "Noto Sans CJK SC", "Microsoft YaHei",
  Roboto, sans-serif; line-height: 1.6; color: #1f2328; }}
h1 {{ font-size: 1.6rem; margin: 0 0 .3rem; }}
h2 {{ font-size: 1.15rem; margin: 2rem 0 .6rem; border-bottom: 1px solid #d8dee4;
  padding-bottom: .25rem; }}
.muted {{ color: #57606a; }}
.cards {{ display: flex; flex-wrap: wrap; gap: .75rem; margin: 1.2rem 0 1.5rem; }}
.card {{ flex: 1 1 9rem; border: 1px solid #d8dee4; border-radius: 8px;
  padding: .7rem .9rem; background: #f6f8fa; }}
.card .k {{ font-size: .8rem; color: #57606a; }}
.card .v {{ font-size: 1.4rem; font-weight: 600; }}
.card.ok .v {{ color: #1a7f37; }} .card.bad .v {{ color: #cf222e; }}
table {{ width: 100%; border-collapse: collapse; font-size: .92rem; }}
th, td {{ border: 1px solid #d0d7de; padding: .4rem .6rem; text-align: left;
  vertical-align: top; }}
th {{ background: #f6f8fa; }}
td.name {{ font-family: "Cascadia Code", Consolas, monospace; }}
td.num {{ text-align: right; white-space: nowrap; }}
.badge {{ display: inline-block; padding: .05rem .45rem; border-radius: 999px;
  font-size: .78rem; font-weight: 600; }}
.badge.ok {{ background: #dafbe1; color: #1a7f37; }}
.badge.bad {{ background: #ffebe9; color: #cf222e; }}
.badge.skip {{ background: #fff8c5; color: #9a6700; }}
code {{ font-size: .85em; }}
</style>
</head>
<body>
<h1>性能测试报告</h1>
<div class="muted">{datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")}
 · commit <code>{html.escape(commit)}</code> · profile <code>{html.escape(profile)}</code></div>
<div class="cards">
  <div class="card ok"><div class="k">通过</div><div class="v">{ok}</div></div>
  <div class="card bad"><div class="k">失败</div><div class="v">{failed}</div></div>
  <div class="card"><div class="k">跳过</div><div class="v">{skipped}</div></div>
  <div class="card"><div class="k">总耗时</div><div class="v">{elapsed:.0f}s</div></div>
  <div class="card"><div class="k">峰值 RSS</div><div class="v">{peak:.0f}MB</div></div>
</div>
<div class="muted">预算 {budget:.0f}s（上限；各 suite 内部还有 --budget，实际通常更短）。
  环境：{html.escape(platform.platform())} · Python {platform.python_version()}</div>
<h2>suite 结果</h2>
<table>
<thead><tr><th>状态</th><th>suite</th><th>tier</th><th>耗时 s</th><th>峰值 MB</th><th>备注 / 日志</th></tr></thead>
<tbody>
{chr(10).join(trs)}
</tbody>
</table>
<div class="muted" style="margin-top:2rem">由 <code>scripts/bench.py</code> 生成；
suite 与预算定义见 <code>bench/suites.json</code>，使用说明见
<code>docs/benchmarking.md</code>。</div>
</body>
</html>
"""


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="统一性能测试入口（nex + contrib，可组合，带时间/内存预算）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("用法：", 1)[1] if "用法：" in __doc__ else None,
    )
    ap.add_argument("--list", action="store_true", help="列出 suite（不改动）")
    ap.add_argument("--markdown", action="store_true", help="--list/结果输出 Markdown")
    ap.add_argument("--modules", metavar="A,B", help="按 nex 模块选择（如 bigint_bin,ntt）")
    ap.add_argument("--contrib", metavar="A,B", help="按 contrib 选择（如 nex_opencl）")
    ap.add_argument("--all", action="store_true", help="选择档位内全部 suite")
    ap.add_argument("--exclude", action="append", default=[], metavar="GLOB",
                    help="排除 suite 名（可重复）")
    ap.add_argument("--suite", action="append", default=[], metavar="NAME",
                    help="只跑指定 suite（可重复；优先于 --modules/--contrib）")
    ap.add_argument("--suite-args", action="append", default=[],
                    metavar="NAME=ARGS",
                    help="给指定 suite 追加参数（可重复；${json} 仍生效）")
    ap.add_argument("--profile", choices=sorted(PROFILES), default="quick",
                    help="预算档：ci=300s / quick=1200s / full=7200s")
    ap.add_argument("--max-time", type=float, default=0.0,
                    help="全局时间预算（秒），覆盖档位")
    ap.add_argument("--max-rss-mb", type=int, default=0,
                    help="全局内存上限（MB），与 suite 限制取小")
    ap.add_argument("--include-frozen", action="store_true",
                    help="包含 frozen 项（SSA/OOC）")
    ap.add_argument("--no-build", action="store_true", help="不构建，只用现成产物")
    ap.add_argument("--build-root", default="build", help="构建根（默认 build）")
    ap.add_argument("--out-dir", default="build/bench",
                    help="日志/结果目录（默认 build/bench）")
    ap.add_argument("--json", metavar="FILE",
                    help="结果写 JSON（含 profile/budget/results）")
    ap.add_argument("--markdown-out", metavar="FILE", help="结果写 Markdown 表")
    ap.add_argument("--html-out", metavar="FILE",
                    help="结果写自包含 HTML 报告")
    ap.add_argument("--dry-run", action="store_true", help="只显示将运行的 suite")
    args = ap.parse_args(argv)

    suites = load_suites()
    if args.list:
        list_table(suites, args.markdown)
        return 0

    chosen = select(suites, args)

    overrides = {}
    all_names = {x["name"] for x in suites}
    for item in args.suite_args:
        if "=" not in item:
            sys.exit(f"--suite-args 需要 NAME=ARGS：{item}")
        name, raw = item.split("=", 1)
        name = name.strip()
        if name not in all_names:
            sys.exit(f"--suite-args 未知 suite：{name}")
        overrides.setdefault(name, []).extend(shlex.split(raw))
    for s in chosen:
        if s["name"] in overrides:
            s["args"] = list(s["args"]) + overrides[s["name"]]

    if not chosen:
        print("没有匹配的 suite（检查 --modules/--contrib/--profile）")
        return 0

    build_root = ROOT / args.build_root
    out_dir = ROOT / args.out_dir

    budget = args.max_time or PROFILES[args.profile]
    print(f"== profile={args.profile} budget={budget:.0f}s suites={len(chosen)}")
    if args.dry_run:
        for s in chosen:
            print(f"  {s['tier']:<7} est={s['est_s']:>5}s  {s['name']}"
                  f"  args: {' '.join(s['args'])}")
        total = sum(s["est_s"] for s in chosen)
        print(f"预估合计 {total}s（预算 {budget:.0f}s）")
        return 0

    build_errors: dict[str, str] = {}
    if not args.no_build:
        build_errors = build_suites(chosen, build_root)

    deadline = time.monotonic() + budget
    started = time.monotonic()
    rows: list[dict] = []
    for i, s in enumerate(chosen, 1):
        remaining = deadline - time.monotonic()
        if remaining <= 1.0:
            rows.append({"suite": s["name"], "tier": s["tier"],
                         "status": "skipped", "seconds": 0.0,
                         "peak_rss_mb": 0, "rc": None, "log": None,
                         "json": None, "reason": "budget"})
            continue
        if s["preset"] in build_errors:
            rec = {"suite": s["name"], "tier": s["tier"],
                   "status": "skip(optional)" if s["optional"] else "fail",
                   "seconds": 0.0, "peak_rss_mb": 0, "rc": None, "log": None,
                   "json": None, "reason": build_errors[s["preset"]]}
            rows.append(rec)
            continue
        print(f"[{i}/{len(chosen)}] {s['name']} ...", flush=True)
        rec = run_suite(s, args, build_root, out_dir, remaining)
        rows.append(rec)
        print(f"  {rec['status']:<14} {rec['seconds']:>8.1f}s "
              f"{rec['peak_rss_mb']:>7.0f}MB"
              + (f"  ({rec['reason']})" if rec["reason"] else ""), flush=True)

    print()
    print_table(rows, args.markdown)

    ok = sum(r["status"] == "ok" for r in rows)
    skipped = sum(r["status"].startswith("skip") for r in rows)
    failed = [r for r in rows if r["status"] in ("fail", "timeout", "rss")]
    print(f"== 完成：ok={ok} skipped={skipped} failed={len(failed)} "
          f"耗时 {time.monotonic() - started:.0f}s")

    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as f:
            f.write(json.dumps({"profile": args.profile, "budget_s": budget,
                                "results": rows}, ensure_ascii=False) + "\n")
    if args.markdown_out:
        Path(args.markdown_out).parent.mkdir(parents=True, exist_ok=True)
        with open(args.markdown_out, "w", encoding="utf-8") as f:
            print("| suite | tier | 状态 | 耗时 s | 峰值 MB |", file=f)
            print("|---|---|---|---:|---:|", file=f)
            for r in rows:
                print(f"| `{r['suite']}` | {r['tier']} | {r['status']} | "
                      f"{r['seconds']:.1f} | {r['peak_rss_mb']:.0f} |", file=f)
    if args.html_out:
        out = Path(args.html_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(render_html(rows, args.profile, budget,
                                   time.monotonic() - started),
                       encoding="utf-8", newline="\n")
        print(f"== HTML: {out}")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
