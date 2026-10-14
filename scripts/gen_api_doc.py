#!/usr/bin/env python3
"""从公开头文件生成参考手册的 API 章节（Markdown）。

头文件中的 `brief/param/return/note` 注释是唯一事实来源；本脚本把它们连同
声明提取为 Markdown，避免手写与漂移。

用法：
    python3 scripts/gen_api_doc.py [输出目录]      # 默认 docs/manual
    python3 scripts/gen_api_doc.py --check         # 只检查并报告（不写文件）
"""
import os
import re
import sys

# (头文件, 输出文件名, 标题) —— 顺序即手册中的顺序
MODULES = [
    ("nex/bigint/nex_bigint_common.h", "api-00-common",
     "共享类型与错误码"),
    ("nex/bigint/bin/nex_bigint_bin.h", "api-10-bigint-bin",
     "bigint_bin —— 二进制肢任意精度整数"),
    ("nex/bigint/bin64/nex_bigint_bin64.h", "api-11-bigint-bin64",
     "bigint_bin64 —— 64 位肢任意精度整数"),
    ("nex/bigint/dec/nex_bigint_dec.h", "api-12-bigint-dec",
     "bigint_dec —— 十进制肢任意精度整数"),
    ("nex/bigint/nex_bigint_conv.h", "api-13-bigint-conv",
     "bigint_conv —— bin ↔ dec 互转"),
    ("nex/bigfrac/nex_bigfrac.h", "api-20-bigfrac",
     "bigfrac —— 任意精度有理数"),
    ("nex/bigfloat/nex_bigfloat.h", "api-30-bigfloat",
     "bigfloat —— 可调精度二进制浮点"),
    ("nex/bigdecimal/nex_bigdecimal.h", "api-31-bigdecimal",
     "bigdecimal —— 可调精度十进制浮点"),
    ("nex/bigcomplex/float/nex_bigcomplex_float.h", "api-40-bigcomplex-float",
     "bigcomplex_float —— 二进制浮点复数"),
    ("nex/bigcomplex/decimal/nex_bigcomplex_decimal.h", "api-41-bigcomplex-decimal",
     "bigcomplex_decimal —— 十进制浮点复数"),
    ("nex/convert/nex_convert.h", "api-50-convert",
     "convert —— 跨类型转换矩阵"),
    ("nex/series/nex_series.h", "api-55-series",
     "series —— 幂级数求值（精确系数 + 全局尾界）"),
    ("nex/fft/nex_fft.h", "api-60-fft",
     "fft —— 浮点复数 FFT 核心（低层）"),
    ("nex/ntt/nex_ntt.h", "api-61-ntt",
     "ntt —— 数论变换核心（低层）"),
    ("nex/nex_alloc.h", "api-70-alloc",
     "nex_alloc —— 内部统一分配器"),
]

KEYWORD_RE = re.compile(r"^(brief|param|return|note):\s*(.*)$")
WARNINGS = []


def tokenize(text):
    """把源码分成 ('comment', [行]) 与 ('code', 行) 两种 token。"""
    tokens = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        start = line.find("/*")
        if start < 0:
            tokens.append(("code", line))
            i += 1
            continue
        end = line.find("*/", start + 2)
        if end >= 0:
            tokens.append(("code", line[:start]))
            tokens.append(("comment", [line[start + 2:end]]))
            tokens.append(("code", line[end + 2:]))
            i += 1
            continue
        tokens.append(("code", line[:start]))
        inner = [line[start + 2:]]
        i += 1
        while i < len(lines) and lines[i].find("*/") < 0:
            inner.append(lines[i])
            i += 1
        if i < len(lines):
            close = lines[i].find("*/")
            inner.append(lines[i][:close])
            tokens.append(("comment", inner))
            tokens.append(("code", lines[i][close + 2:]))
        i += 1
    return tokens


def clean_comment(inner):
    out = []
    for raw in inner:
        s = raw.strip()
        if s.startswith("*"):
            s = s[1:]
        out.append(s.strip())
    return out


def parse_doc(lines):
    fields = {"brief": "", "params": [], "return": "", "note": ""}
    cur = None
    for line in lines:
        match = KEYWORD_RE.match(line)
        if match:
            key, val = match.group(1), match.group(2)
            if key == "param":
                fields["params"].append(val)
            else:
                fields[key] = val
            cur = key
        elif line:
            if cur == "param" and fields["params"]:
                fields["params"][-1] += " " + line
            elif cur:
                fields[cur] += " " + line
    return fields if fields["brief"] else None


def render_doc(doc, sig):
    name = re.search(r"\b([A-Za-z_]\w*)\s*\(", sig)
    heading = name.group(1) if name else sig[:48]
    out = ["### `%s`" % heading, "", "```c", sig, "```", ""]
    out.append("**说明**：%s" % doc["brief"])
    out.append("")
    if doc["params"]:
        out.append("**参数**")
        for param in doc["params"]:
            parts = param.split(None, 1)
            pname = parts[0].strip("`") if parts else ""
            pdesc = parts[1] if len(parts) > 1 else ""
            out.append("- `%s`：%s" % (pname, pdesc))
        out.append("")
    if doc["return"]:
        out.append("**返回**：%s" % doc["return"])
        out.append("")
    if doc["note"]:
        out.append("**备注**：%s" % doc["note"])
        out.append("")
    return out


def normalize(run):
    return re.sub(r"\s+", " ", " ".join(x.strip() for x in run)).strip()


def emit_code(out, run, doc, path):
    joined = normalize(run)
    if not joined:
        return
    is_type = joined.startswith("typedef") and "(" not in joined.split("}")[-1]
    if doc is not None:
        if is_type:
            name = re.search(r"\}\s*([A-Za-z_]\w*)\s*;", joined)
            out.append("### `%s`" % (name.group(1) if name else "type"))
            out.append("")
            out.append("**说明**：%s" % doc["brief"])
            out.append("")
            out.append("```c")
            out.append("\n".join(x for x in run if x.strip()))
            out.append("```")
            out.append("")
        else:
            out.extend(render_doc(doc, joined))
        return
    if is_type:
        body = "\n".join(x for x in run if x.strip())
        out.append("```c")
        out.append(body)
        out.append("```")
        out.append("")
    elif "(" in joined:
        WARNINGS.append("%s: 未文档化函数/声明：%s" % (path, joined[:70]))
        out.append("```c")
        out.append(joined)
        out.append("```")
        out.append("")
    # 其余（宏、extern 变量等）忽略


def gen_header(path, title):
    text = open(path, encoding="utf-8", errors="ignore").read()
    tokens = tokenize(text)
    out = ["# %s" % title, ""]
    first_comment = True
    last_was_sep = False
    pending_doc = None
    code_run = []
    depth = 0

    for kind, val in tokens:
        if kind == "comment":
            lines = clean_comment(val)
            joined = " ".join(lines).strip()
            if joined and set(joined) <= set("-"):
                last_was_sep = True
                continue
            doc = parse_doc(lines)
            if doc is not None:
                emit_code(out, code_run, pending_doc, path)
                code_run = []
                depth = 0
                pending_doc = doc
                last_was_sep = False
                first_comment = False
                continue
            if last_was_sep and joined:
                out.append("## %s" % joined)
                out.append("")
                last_was_sep = False
                first_comment = False
                continue
            if first_comment and joined:
                out.extend(lines)
                out.append("")
            elif joined:
                emit_code(out, code_run, pending_doc, path)
                code_run = []
                depth = 0
                out.append(joined)
                out.append("")
            last_was_sep = False
            first_comment = False
        else:
            stripped = val.strip()
            if not stripped:
                continue
            if stripped.startswith(("#include", "#ifndef", "#define", "#endif",
                                    "#ifdef", "extern \"C\"", "#if")):
                continue
            if stripped in ("{", "}") and not code_run and depth == 0:
                continue
            code_run.append(stripped)
            depth += stripped.count("{") - stripped.count("}")
            if depth <= 0 and ";" in stripped:
                emit_code(out, code_run, pending_doc, path)
                code_run = []
                depth = 0
                pending_doc = None
    emit_code(out, code_run, pending_doc, path)
    return "\n".join(out).rstrip() + "\n"


def main():
    check_only = "--check" in sys.argv
    out_dir = "docs/manual"
    for arg in sys.argv[1:]:
        if not arg.startswith("--"):
            out_dir = arg
    if not check_only:
        os.makedirs(out_dir, exist_ok=True)
    for path, slug, title in MODULES:
        md = gen_header(path, title)
        if not check_only:
            with open(os.path.join(out_dir, slug + ".md"), "w",
                      encoding="utf-8", newline="\n") as fh:
                fh.write(md)
    for warning in WARNINGS:
        print("WARN " + warning, file=sys.stderr)
    if not check_only:
        print("已生成 %d 个 API 页面到 %s/" % (len(MODULES), out_dir))
    if check_only and WARNINGS:
        sys.exit(1)


if __name__ == "__main__":
    main()
