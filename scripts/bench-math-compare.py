#!/usr/bin/env python3
"""bench_math 夜间趋势对比（advisory）。

比较当前 `bench_math --preset ci --json` 与仓库基线
（默认 `bench/baseline/bench-math-ci.json`），输出 Markdown 报告。
默认永远退出 0（先趋势、后门禁）；可用 `--fail-over` 显式启用门禁。

JSON Lines 记录：
  {"meta":"bench_math", ...}
  {"section":"trans","type":"bigfloat","op":"exp","precision":64,"ms":...}
  {"section":"throughput","op":"bigfloat_mul","unit":"bit","size":256,
   "ops_per_sec":...}

阈值与噪声：
  - 默认相对阈值 20%：slowdown ≥ 1.20 记为回归，≤ 1/1.20 记为改善；
  - 绝对值噪声地板：ms < 0.01 ms（或 ops/s > 1e5，即单次 < 10 µs）的项
    不参与门禁判断，但仍列在“低噪声”表；
  - 基线/当前必须来自同一 runner 环境；不同机器只用于人工参考。
"""

import argparse
import json
import os
import sys


def load_jsonl(path):
    measures = {}
    through = {}
    meta = None
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("meta") == "bench_math":
                meta = rec
                continue
            if "ms" in rec:
                key = (rec.get("section", ""), rec.get("type", ""),
                       rec.get("op", ""), rec.get("precision"))
                try:
                    value = float(rec["ms"])
                except (TypeError, ValueError):
                    continue
                if key not in measures or value < measures[key]:
                    measures[key] = value
            elif "ops_per_sec" in rec:
                key = (rec.get("section", ""), rec.get("op", ""),
                       rec.get("unit", ""), rec.get("size"))
                try:
                    value = float(rec["ops_per_sec"])
                except (TypeError, ValueError):
                    continue
                if key not in through or value > through[key]:
                    through[key] = value
    return meta, measures, through


class Row:
    def __init__(self, kind, key, base, cur, slowdown, noise):
        self.kind = kind
        self.key = key
        self.base = base
        self.cur = cur
        self.slowdown = slowdown
        self.noise = noise

    def change_pct(self):
        return (self.slowdown - 1.0) * 100.0

    def spec(self):
        section, op, sub, size = self.key
        if self.kind == "ms":
            return "%s%s @ %s" % (op, ("/" + sub) if sub else "", size)
        return "%s @ %s %s" % (op, size, sub)


def fmt_value(row):
    if row.kind == "ms":
        return "%.6g ms" % row.cur
    return "%.6g ops/s" % row.cur


def fmt_base(row):
    if row.kind == "ms":
        return "%.6g ms" % row.base
    return "%.6g ops/s" % row.base


def build_rows(base_meta, base_ms, base_ops, cur_meta, cur_ms, cur_ops):
    rows = []
    for key, cur in cur_ms.items():
        base = base_ms.get(key)
        if base is None or base <= 0.0 or cur <= 0.0:
            continue
        slowdown = cur / base
        rows.append(Row("ms", key, base, cur, slowdown, base < 0.01))
    for key, cur in cur_ops.items():
        base = base_ops.get(key)
        if base is None or base <= 0.0 or cur <= 0.0:
            continue
        slowdown = base / cur  # ops/s 下降 = slowdown > 1
        rows.append(Row("ops", key, base, cur, slowdown, base > 1e5))
    return rows


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline",
                    default=os.path.join(repo, "bench", "baseline",
                                         "bench-math-ci.json"))
    ap.add_argument("--current", required=True)
    ap.add_argument("--out", default=None,
                    help="Markdown 输出路径；缺省写 stdout")
    ap.add_argument("--threshold", type=float, default=0.20,
                    help="相对阈值（默认 0.20 = 20%%）")
    ap.add_argument("--fail-over", type=float, default=None,
                    help="可选门禁：非低噪声项最大 slowdown 超过该值时退出 1")
    args = ap.parse_args(argv)

    lines = []
    if not os.path.exists(args.baseline):
        lines.append("# bench_math 趋势对比（advisory）\n")
        lines.append("未找到基线文件 `%s`。\n" % args.baseline)
        lines.append("当前运行结果 `%s` 可作为首个基线候选；"
                     "确认 runner 环境一致后提交到 "
                     "`bench/baseline/bench-math-ci.json`。\n" % args.current)
        report = "\n".join(lines)
        if args.out:
            with open(args.out, "w", encoding="utf-8") as f:
                f.write(report + "\n")
        else:
            sys.stdout.write(report + "\n")
        return 0

    base_meta, base_ms, base_ops = load_jsonl(args.baseline)
    cur_meta, cur_ms, cur_ops = load_jsonl(args.current)
    rows = build_rows(base_meta, base_ms, base_ops, cur_meta, cur_ms, cur_ops)

    threshold = max(0.0, args.threshold)
    up = 1.0 + threshold
    down = 1.0 / up
    regress = sorted([r for r in rows if r.slowdown >= up and not r.noise],
                     key=lambda r: r.slowdown, reverse=True)
    improve = sorted([r for r in rows if r.slowdown <= down and not r.noise],
                     key=lambda r: r.slowdown)
    noise = sorted([r for r in rows if r.noise and
                    (r.slowdown >= up or r.slowdown <= down)],
                   key=lambda r: r.slowdown, reverse=True)
    missing = sorted(set(base_ms) - set(cur_ms)) + \
        sorted(set(base_ops) - set(cur_ops))
    added = sorted(set(cur_ms) - set(base_ms)) + \
        sorted(set(cur_ops) - set(base_ops))

    lines.append("# bench_math 趋势对比（advisory）\n")
    lines.append("| 项目 | 基线 | 当前 |")
    lines.append("|---|---|---|")
    lines.append("| 基线元信息 | `%s` | — |" % json.dumps(base_meta,
                                                          ensure_ascii=False))
    lines.append("| 当前元信息 | — | `%s` |" % json.dumps(cur_meta,
                                                        ensure_ascii=False))
    lines.append("| 匹配项 | %d | — |" % len(rows))
    lines.append("| 回归 / 改善 / 低噪声 | %d / %d / %d | 阈值 ±%.0f%% |"
                 % (len(regress), len(improve), len(noise), threshold * 100))
    lines.append("| 缺失 / 新增 | %d / %d | — |"
                 % (len(missing), len(added)))
    lines.append("")

    def emit_table(title, items, reverse=False):
        lines.append("## %s\n" % title)
        if not items:
            lines.append("（无）\n")
            return
        lines.append("| section | 运算 | 基线 | 当前 | 变化 |")
        lines.append("|---|---|---:|---:|---:|")
        for r in items[:20]:
            lines.append("| %s | %s | %s | %s | %+.1f%% |"
                         % (r.key[0], r.spec(), fmt_base(r), fmt_value(r),
                            r.change_pct()))
        if len(items) > 20:
            lines.append("\n（仅列前 20 项，共 %d 项）\n" % len(items))
        lines.append("")

    emit_table("回归（≥ +%.0f%%）" % (threshold * 100), regress)
    emit_table("改善（≤ −%.0f%%）" % (threshold * 100), improve)
    if noise:
        emit_table("低噪声（|Δ| 超阈值，但不参与门禁）", noise)
    if missing:
        lines.append("## 基线中缺失的项（当前未运行/被预算截断）\n")
        for k in missing[:20]:
            lines.append("- `%s %s @ %s`" % (k[0], k[1], k[3]))
        lines.append("")
    if added:
        lines.append("## 新增项（基线中无）\n")
        for k in added[:20]:
            lines.append("- `%s %s @ %s`" % (k[0], k[1], k[3]))
        lines.append("")

    lines.append("> 说明：默认仅趋势、不阻断 CI。低噪声项绝对值 < 10 µs；"
                 "跨 runner/机器的对比只作人工参考。\n")
    report = "\n".join(lines)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(report + "\n")
    else:
        sys.stdout.write(report + "\n")

    if args.fail_over is not None:
        worst = max([r.slowdown for r in rows if not r.noise] or [1.0])
        if worst > args.fail_over:
            sys.stderr.write("bench-math-compare: worst slowdown %.2fx > "
                             "%.2fx\n" % (worst, args.fail_over))
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
