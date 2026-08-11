#!/usr/bin/env python3
"""bigcomplex（float 版）黄金对拍：随机生成测试用例，C 驱动 vs Python
参考。参考复用 golden_bigfloat.py 的标量语义（Fraction 精确复刻），
按设计 §9 的分量组合规则（乘法朴素四乘二加、除法共轭法、NaN 传播
§9.3）组合成复数运算。

用法：
    python golden_bigcomplex_float.py [driver_path]

driver_path 默认为 ./golden_bigcomplex_float_driver；可通过环境变量
NEX_GOLDEN_DRIVER 覆盖。全部通过打印 GOLDEN OK 并退出 0。
"""
import os
import random
import re
import subprocess
import sys

# 复用标量 golden 参考（同目录）
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import golden_bigfloat as gbf

DRIVER = os.environ.get(
    "NEX_GOLDEN_DRIVER",
    sys.argv[1] if len(sys.argv) > 1 else "./golden_bigcomplex_float_driver")

RAND_SEED = int(os.environ.get("NEX_GOLDEN_SEED", "20260812"))

F_POS_ZERO, F_NEG_ZERO, F_POS, F_NEG, F_POS_INF, F_NEG_INF, F_NAN = range(7)
ZERO = (F_POS_ZERO, 0, 0)


def parse_scalar_prefix(s, ctx):
    """复刻 C 的 bigfloat_from_str 部分消费：返回 (值, 已消费长度) 或 None。"""
    m = re.match(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', s)
    if m is None:
        m2 = re.match(r'[+-]?(?:inf|nan)', s)
        if m2 is None:
            return None
        return gbf.parse_decimal(m2.group(0), ctx), len(m2.group(0))
    tok = m.group(0)
    return gbf.parse_decimal(tok, ctx), len(tok)


def parse_complex(s, ctx):
    """复数串 → ((re, im), 已消费长度)；非法返回 None。"""
    r = parse_scalar_prefix(s, ctx)
    if r is None:
        return None
    re_val, clen = r
    rest = s[clen:]
    if rest.startswith("i"):
        im_val = re_val
        re_val = ZERO
        return (re_val, im_val), clen + 1
    if rest[:1] in ("+", "-"):
        sign = rest[0]
        r2 = parse_scalar_prefix(rest[1:], ctx)
        if r2 is None:
            return None
        im_val, clen2 = r2
        if not rest[1 + clen2:].startswith("i"):
            return None
        if sign == "-":
            im_val = gbf.neg(im_val)
        return (re_val, im_val), clen + 1 + clen2 + 1
    return (re_val, ZERO), clen


def cadd(a, b, ctx):
    return (gbf.add(a[0], b[0], ctx), gbf.add(a[1], b[1], ctx))


def csub(a, b, ctx):
    return (gbf.sub(a[0], b[0], ctx), gbf.sub(a[1], b[1], ctx))


def cmul(a, b, ctx):
    ac = gbf.mul(a[0], b[0], ctx)
    bd = gbf.mul(a[1], b[1], ctx)
    bc = gbf.mul(a[1], b[0], ctx)
    ad = gbf.mul(a[0], b[1], ctx)
    return (gbf.sub(ac, bd, ctx), gbf.add(bc, ad, ctx))


def cdiv(a, b, ctx):
    re_num = gbf.add(gbf.mul(a[0], b[0], ctx), gbf.mul(a[1], b[1], ctx), ctx)
    im_num = gbf.sub(gbf.mul(a[1], b[0], ctx), gbf.mul(a[0], b[1], ctx), ctx)
    den = gbf.add(gbf.mul(b[0], b[0], ctx), gbf.mul(b[1], b[1], ctx), ctx)
    return (gbf.div(re_num, den, ctx), gbf.div(im_num, den, ctx))


def cabs(a, ctx):
    r2 = gbf.mul(a[0], a[0], ctx)
    i2 = gbf.mul(a[1], a[1], ctx)
    return gbf.sqrt(gbf.add(r2, i2, ctx), ctx)


def cconj(a):
    return (a[0], gbf.neg(a[1]))


def ceq(a, b):
    return gbf.cmp(a[0], b[0]) == 0 and gbf.cmp(a[1], b[1]) == 0

def scalar_same(a, b):
    # NaN 与任何值（含自身）不相等：两侧均为 NaN 视为一致
    if a[0] == F_NAN or b[0] == F_NAN:
        return a[0] == F_NAN and b[0] == F_NAN
    return gbf.cmp(a, b) == 0  # 标量语义（含 +0 == −0）


def ceq_roundtrip(a, b):
    return scalar_same(a[0], b[0]) and scalar_same(a[1], b[1])



def rand_scalar(rng, eb):
    nd = rng.randrange(1, 25)
    digits = str(rng.randrange(1, 10)) + "".join(
        str(rng.randrange(10)) for _ in range(nd - 1))
    e10 = rng.randrange(-eb, eb + 1)
    if rng.randrange(3) == 0:
        return "%s.%se%d" % (digits[:1], digits[1:], e10)
    return "%se%d" % (digits, e10)


def rand_complex(rng, eb):
    mode = rng.randrange(3)
    a = rand_scalar(rng, eb)
    if mode == 0:
        return a
    if mode == 1:
        return a + "i"
    b = rand_scalar(rng, eb)
    sep = rng.choice(["+", "-"])
    return "%s%s%si" % (a, sep, b)


def fmt_value(z):
    return "%d %d %d %d %d %d" % (z[0][0], z[0][1], z[0][2],
                                  z[1][0], z[1][1], z[1][2])


def fmt_scalar(v):
    return "%d %d %d" % v


def run_driver(lines):
    proc = subprocess.run([DRIVER], input="\n".join(lines),
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print("driver exit %d" % proc.returncode)
        print(proc.stderr)
        sys.exit(1)
    return proc.stdout.splitlines()


def main():
    rng = random.Random(RAND_SEED)
    lines = []
    expected = []
    tstr_ctx = {}   # 行号 → (期望值, ctx)，供 bcf_tstr 回环验证
    cur_ctx = None

    def emit(line, want):
        lines.append(line)
        expected.append(want)

    def emit_tstr(s, v, ctx):
        lines.append("bcf_tstr %s" % s)
        expected.append("R tstr")
        tstr_ctx[len(lines) - 1] = (v, ctx)

    def set_ctx(ctx):
        nonlocal cur_ctx
        cur_ctx = ctx
        emit("bcf_ctx %d %d %d" % (ctx.p, ctx.exp_bits, ctx.mode), "R ok")

    # 随机用例
    for _ in range(350):
        p, eb, mode = (rng.choice([24, 53, 113]), rng.choice([8, 11, 15]),
                       rng.randrange(5))
        c = gbf.Ctx(p, eb, mode)
        sa = rand_complex(rng, max(1, eb - 1))
        sb = rand_complex(rng, max(1, eb - 1))
        va = parse_complex(sa, c)[0]
        vb = parse_complex(sb, c)[0]
        set_ctx(c)
        emit("bcf_parse %s" % sa, "R " + fmt_value(va))
        emit("bcf_parse %s" % sb, "R " + fmt_value(vb))
        emit("bcf_add %s %s" % (sa, sb), "R " + fmt_value(cadd(va, vb, c)))
        emit("bcf_sub %s %s" % (sa, sb), "R " + fmt_value(csub(va, vb, c)))
        emit("bcf_mul %s %s" % (sa, sb), "R " + fmt_value(cmul(va, vb, c)))
        emit("bcf_div %s %s" % (sa, sb), "R " + fmt_value(cdiv(va, vb, c)))
        emit("bcf_abs %s" % sa, "R " + fmt_scalar(cabs(va, c)))
        emit("bcf_conj %s" % sa, "R " + fmt_value(cconj(va)))
        emit("bcf_eq %s %s" % (sa, sb), "R %d" % (1 if ceq(va, vb) else 0))
        emit_tstr(sa, va, c)

    # 特殊值与边界
    c = gbf.Ctx(53, 11, 0)
    set_ctx(c)
    for s in ["0", "1+1i", "inf+infi", "-inf-inf*i", "nan+1i", "1+nani",
              "0+infi", "inf-0i", "3.5", "2.5i", "-2.5i",
              "1e300+1e-300i", "0.1+0.2i", "1-infi", "inf-inf*i"]:
        pc = parse_complex(s, c)
        if pc is None:
            emit("bcf_parse %s" % s, "E parse")
            continue
        emit("bcf_parse %s" % s, "R " + fmt_value(pc[0]))
        emit("bcf_mul %s %s" % (s, "1+0i"), "R " + fmt_value(cmul(pc[0],
             ((F_POS, 1, 0), ZERO), c)))
        emit("bcf_abs %s" % s, "R " + fmt_scalar(cabs(pc[0], c)))
        emit_tstr(s, pc[0], c)

    outs = run_driver(lines)
    if len(outs) != len(expected):
        print("line count mismatch: %d vs %d" % (len(outs), len(expected)))
        sys.exit(1)
    fails = 0
    for i, (line, out, want) in enumerate(zip(lines, outs, expected)):
        if line.startswith("bcf_tstr"):
            ev, ctx = tstr_ctx[i]
            s_out = out[2:] if out.startswith("R ") else ""
            # to_str 的往返保证是最近偶（同精度）下的，用最近偶 ctx 回环
            vctx = gbf.Ctx(ctx.p, ctx.exp_bits, 0)
            r = parse_complex(s_out, vctx)
            if r is None or not ceq_roundtrip(r[0], ev):
                print("MISMATCH(tstr): %s: got %r" % (line, out))
                fails += 1
            continue
        if out != want:
            print("MISMATCH: %s: got %r want %r" % (line, out, want))
            fails += 1
            if fails > 20:
                print("...")
                break
    if fails:
        print("GOLDEN FAIL: %d mismatches" % fails)
        sys.exit(1)
    print("GOLDEN OK")


if __name__ == "__main__":
    main()
