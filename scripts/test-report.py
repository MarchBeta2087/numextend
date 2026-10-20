#!/usr/bin/env python3
"""运行 CTest 并生成自包含 HTML 测试报告（无第三方依赖）。

用法：
    python3 scripts/test-report.py                       # build/default -> build/test-report.html
    python3 scripts/test-report.py --build-dir build/opencl --out build/opencl-report.html
    python3 scripts/test-report.py -R 'bigint|ntt'       # 透传给 ctest 的过滤参数

要求：CMake/CTest >= 3.21（JUnit 输出）。报告写入 --out（默认 build/test-report.html），
不进入仓库；脚本本身可复用。退出码与 ctest 一致。
"""

from __future__ import annotations

import argparse
import html
import os
import platform
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                          errors="replace")


def git_info() -> tuple[str, bool]:
    rev = run(["git", "rev-parse", "--short", "HEAD"], ROOT)
    dirty = run(["git", "status", "--porcelain"], ROOT)
    return (rev.stdout.strip() or "unknown",
            bool(dirty.stdout.strip()))


def cpu_name() -> str:
    if sys.platform.startswith("linux"):
        try:
            for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    elif sys.platform == "darwin":
        out = run(["sysctl", "-n", "machdep.cpu.brand_string"])
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    elif os.name == "nt":
        name = os.environ.get("PROCESSOR_IDENTIFIER", "")
        if name:
            return name
    return platform.processor() or platform.machine() or "unknown"


def build_info(build_dir: Path) -> dict[str, str]:
    info = {"build_type": "?", "compiler": "?", "compiler_version": "?"}
    cache = build_dir / "CMakeCache.txt"
    try:
        text = cache.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return info
    m = re.search(r"^CMAKE_BUILD_TYPE:STRING=(.*)$", text, re.M)
    if m:
        info["build_type"] = m.group(1) or "(default)"
    m = re.search(r"^CMAKE_C_COMPILER:FILEPATH=(.*)$", text, re.M)
    if m:
        cc = m.group(1).strip()
        info["compiler"] = cc
        out = run([cc, "--version"])
        if out.returncode == 0 and out.stdout.strip():
            info["compiler_version"] = out.stdout.splitlines()[0].strip()
    return info


def parse_junit(path: Path) -> tuple[list[dict], dict]:
    tree = ET.parse(path)
    root = tree.getroot()
    cases: list[dict] = []

    def walk(node):
        for child in node:
            if child.tag == "testcase":
                failure = child.find("failure")
                skipped = child.find("skipped")
                status = "pass"
                message = ""
                if failure is not None:
                    status = "fail"
                    message = failure.get("message", "") or (failure.text or "")
                elif skipped is not None:
                    status = "skip"
                    message = skipped.get("message", "") or (skipped.text or "")
                cases.append({
                    "name": child.get("name", "?"),
                    "classname": child.get("classname", ""),
                    "time": float(child.get("time", "0") or 0),
                    "status": status,
                    "message": message.strip(),
                })
            elif child.tag in ("testsuite", "testsuites"):
                walk(child)

    walk(root)
    summary = {
        "total": len(cases),
        "pass": sum(c["status"] == "pass" for c in cases),
        "fail": sum(c["status"] == "fail" for c in cases),
        "skip": sum(c["status"] == "skip" for c in cases),
        "time": sum(c["time"] for c in cases),
    }
    return cases, summary


def badge(status: str) -> str:
    cls = {"pass": "ok", "fail": "bad", "skip": "skip"}[status]
    label = {"pass": "PASS", "fail": "FAIL", "skip": "SKIP"}[status]
    return f'<span class="badge {cls}">{label}</span>'


def render(report: dict) -> str:
    cases = report["cases"]
    s = report["summary"]
    rows = []
    for c in sorted(cases, key=lambda x: x["name"]):
        detail = ""
        if c["message"]:
            detail = f'<details><summary>输出</summary><pre>{html.escape(c["message"])}</pre></details>'
        rows.append(
            f'<tr><td>{badge(c["status"])}</td>'
            f'<td class="name">{html.escape(c["name"])}</td>'
            f'<td class="num">{c["time"]:.2f}s</td>'
            f'<td>{detail}</td></tr>')
    failures = [c for c in cases if c["status"] == "fail"]
    fail_html = ""
    if failures:
        blocks = [f'<h2>失败详情（{len(failures)}）</h2>']
        for c in failures:
            blocks.append(f'<h3>{html.escape(c["name"])}</h3>'
                          f'<pre class="log">{html.escape(c["message"])}</pre>')
        fail_html = "\n".join(blocks)

    env = report["env"]
    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>测试报告 · {html.escape(env["commit"])}</title>
<style>
:root {{ color-scheme: light; }}
* {{ box-sizing: border-box; }}
body {{ margin: 0; padding: 2rem 1.25rem 4rem; font-family: -apple-system,
  "Segoe UI", "Noto Sans CJK SC", "Microsoft YaHei", Roboto, sans-serif;
  line-height: 1.6; color: #1f2328; max-width: 70rem; margin: 0 auto; }}
h1 {{ font-size: 1.6rem; margin: 0 0 .3rem; }}
h2 {{ font-size: 1.2rem; margin: 2rem 0 .6rem; border-bottom: 1px solid #d8dee4;
  padding-bottom: .25rem; }}
h3 {{ font-size: 1rem; margin: 1.2rem 0 .4rem; }}
.muted {{ color: #57606a; }}
.cards {{ display: flex; flex-wrap: wrap; gap: .75rem; margin: 1.2rem 0 1.5rem; }}
.card {{ flex: 1 1 9rem; border: 1px solid #d8dee4; border-radius: 8px;
  padding: .7rem .9rem; background: #f6f8fa; }}
.card .k {{ font-size: .8rem; color: #57606a; }}
.card .v {{ font-size: 1.5rem; font-weight: 600; }}
.card.ok .v {{ color: #1a7f37; }}
.card.bad .v {{ color: #cf222e; }}
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
pre {{ background: #f6f8fa; border: 1px solid #d8dee4; border-radius: 6px;
  padding: .8rem 1rem; overflow-x: auto; font-size: .85rem; }}
pre.log {{ max-height: 28rem; }}
details summary {{ cursor: pointer; color: #0969da; }}
footer {{ margin-top: 2.5rem; font-size: .85rem; }}
</style>
</head>
<body>
<h1>测试报告</h1>
<div class="muted">{html.escape(env["date"])} · commit
  <code>{html.escape(env["commit"])}</code>{' · <b>工作区有未提交改动</b>' if env["dirty"] else ''}</div>

<div class="cards">
  <div class="card"><div class="k">总计</div><div class="v">{s["total"]}</div></div>
  <div class="card ok"><div class="k">通过</div><div class="v">{s["pass"]}</div></div>
  <div class="card bad"><div class="k">失败</div><div class="v">{s["fail"]}</div></div>
  <div class="card"><div class="k">跳过</div><div class="v">{s["skip"]}</div></div>
  <div class="card"><div class="k">耗时</div><div class="v">{s["time"]:.1f}s</div></div>
</div>

<h2>环境</h2>
<table>
<tr><th>构建目录</th><td><code>{html.escape(env["build_dir"])}</code></td></tr>
<tr><th>构建类型</th><td>{html.escape(env["build_type"])}</td></tr>
<tr><th>编译器</th><td>{html.escape(env["compiler"])}<br><span class="muted">{html.escape(env["compiler_version"])}</span></td></tr>
<tr><th>系统</th><td>{html.escape(env["platform"])}</td></tr>
<tr><th>CPU</th><td>{html.escape(env["cpu"])}（{env["cores"]} 逻辑核）</td></tr>
<tr><th>命令</th><td><code>{html.escape(env["command"])}</code></td></tr>
</table>

{fail_html}

<h2>全部用例（{s["total"]}）</h2>
<table>
<thead><tr><th>状态</th><th>用例</th><th>耗时</th><th>备注</th></tr></thead>
<tbody>
{''.join(rows)}
</tbody>
</table>

<footer class="muted">由 <code>scripts/test-report.py</code> 生成；
CTest JUnit 输出 + 自包含 HTML（无外部资源）。</footer>
</body>
</html>
"""


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="运行 CTest 并生成 HTML 测试报告")
    ap.add_argument("--build-dir", default="build/default")
    ap.add_argument("--out", default="build/test-report.html")
    ap.add_argument("--ctest", default="ctest")
    ap.add_argument("extra", nargs="*", help="透传给 ctest 的参数（如 -R bigint）")
    args = ap.parse_args(argv)

    build_dir = (ROOT / args.build_dir).resolve()
    if not (build_dir / "CTestTestfile.cmake").is_file():
        print(f"错误：{build_dir} 不是 CTest 构建目录（先 cmake 配置）",
              file=sys.stderr)
        return 2

    junit = build_dir / "junit-report.xml"
    cmd = [args.ctest, "--test-dir", str(build_dir),
           "--output-on-failure", "--output-junit", str(junit), *args.extra]
    print("== " + " ".join(cmd))
    started = time.monotonic()
    proc = run(cmd, ROOT)
    elapsed = time.monotonic() - started
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)

    if not junit.is_file():
        print("错误：ctest 未生成 JUnit 报告（需要 CMake >= 3.21）",
              file=sys.stderr)
        return proc.returncode or 1

    cases, summary = parse_junit(junit)
    commit, dirty = git_info()
    info = build_info(build_dir)
    env = {
        "date": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "commit": commit, "dirty": dirty,
        "build_dir": str(build_dir.relative_to(ROOT)),
        "build_type": info["build_type"],
        "compiler": info["compiler"],
        "compiler_version": info["compiler_version"],
        "platform": platform.platform(),
        "cpu": cpu_name(),
        "cores": os.cpu_count() or 0,
        "command": " ".join(cmd),
    }
    out = (ROOT / args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render({"cases": cases, "summary": summary, "env": env}),
                   encoding="utf-8", newline="\n")

    print(f"\n== {summary['pass']}/{summary['total']} passed, "
          f"{summary['fail']} failed, {summary['skip']} skipped, "
          f"{elapsed:.1f}s")
    print(f"== HTML: {out}")
    return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
