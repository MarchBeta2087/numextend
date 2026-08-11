#!/usr/bin/env python3
"""bigfrac 黄金对拍：随机生成测试用例，C 驱动 vs Python fractions.Fraction。

用法：
    python golden_bigfrac.py [driver_path]

driver_path 默认为 ./golden_bigfrac_driver；可通过环境变量 NEX_GOLDEN_DRIVER
覆盖。协议见 golden_bigfrac_driver.c 头部注释。全部通过打印 GOLDEN OK 并退出 0。
"""
import os
import random
import subprocess
import sys

# CI/控制台编码容错：非 ASCII 输出以占位符代替，避免 cp1252 等
# 编码环境下 UnicodeEncodeError 崩溃
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")
from fractions import Fraction

DRIVER = os.environ.get(
    "NEX_GOLDEN_DRIVER",
    sys.argv[1] if len(sys.argv) > 1 else "./golden_bigfrac_driver")

RAND_SEED = 20260812
COUNT = 2000


def frac_str(f):
    """Fraction → "p/q"（驱动输入 token 形式）。"""
    return "%d/%d" % (f.numerator, f.denominator)


def frac_out(f):
    """Fraction → "num den"（驱动输出形式）。"""
    return "%d %d" % (f.numerator, f.denominator)


def rand_frac(rng):
    """生成随机既约分数（分母为正）。"""
    n = rng.randrange(-1000, 1001)
    d = rng.randrange(1, 1001)
    return Fraction(n, d)


def rand_decimal(rng):
    """生成随机十进制字符串（整数 / 小数，含符号与尾随零）。"""
    int_part = rng.randrange(-100000, 100001)
    ndigits = rng.randrange(0, 7)
    if ndigits == 0:
        return str(int_part)
    frac_digits = "".join(str(rng.randrange(10)) for _ in range(ndigits))
    return "%d.%s" % (int_part, frac_digits)


def build_cases(rng):
    cases = []
    # 四则 / 比较 / 一元：两个随机分数
    for _ in range(COUNT):
        a = rand_frac(rng)
        b = rand_frac(rng)
        cases.append(("f_add", a, b))
        cases.append(("f_sub", a, b))
        cases.append(("f_mul", a, b))
        cases.append(("f_cmp", a, b))
        if b != 0:
            cases.append(("f_div", a, b))
        cases.append(("f_neg", a))
        cases.append(("f_id", a))
        if a != 0:
            cases.append(("f_inv", a))
    # 大数：40 位十进制随机分数，加与乘（含自运算）
    for _ in range(COUNT // 4):
        n = rng.randrange(-10 ** 40, 10 ** 40)
        d = rng.randrange(1, 10 ** 40)
        a = Fraction(n, d)
        cases.append(("f_add", a, a))
        cases.append(("f_mul", a, a))
        cases.append(("f_div", a, a))
    # from_ints：任意分子分母（含负分母、零分母 → divzero）
    for _ in range(COUNT // 2):
        n = rng.randrange(-1000000, 1000001)
        d = rng.randrange(-1000000, 1000001)
        cases.append(("f_fromints", n, d))
    # from_str：十进制字符串（含尾随零、负号、".5"、特殊错误形态）
    for _ in range(COUNT // 4):
        cases.append(("f_fromstr", rand_decimal(rng)))
    cases.append(("f_fromstr", "0"))
    cases.append(("f_fromstr", "-0.0"))
    cases.append(("f_fromstr", ".5"))
    cases.append(("f_fromstr", "1/0"))   # divzero
    cases.append(("f_fromstr", "/5"))    # parse
    return cases


def expected(op, args):
    """返回 (ok, 结果字符串) 或 (False, 错误标签)。"""
    if op == "f_add":
        return True, frac_out(args[0] + args[1])
    if op == "f_sub":
        return True, frac_out(args[0] - args[1])
    if op == "f_mul":
        return True, frac_out(args[0] * args[1])
    if op == "f_div":
        if args[1] == 0:
            return False, "divzero"
        return True, frac_out(args[0] / args[1])
    if op == "f_cmp":
        a, b = args
        c = (a > b) - (a < b)
        return True, str(c)
    if op == "f_neg":
        return True, frac_out(-args[0])
    if op == "f_inv":
        if args[0] == 0:
            return False, "divzero"
        return True, frac_out(1 / args[0])
    if op == "f_id":
        return True, frac_out(args[0])
    if op == "f_fromints":
        n, d = args
        if d == 0:
            return False, "divzero"
        return True, frac_out(Fraction(n, d))
    if op == "f_fromstr":
        s = args[0]
        if s == "/5":
            return False, "parse"
        if s == "1/0":
            return False, "divzero"
        return True, frac_out(Fraction(s))
    raise AssertionError("unknown op " + op)


def to_token(x):
    """Fraction → "p/q"；其余（int、str）原样字符串化。"""
    if isinstance(x, Fraction):
        return frac_str(x)
    return str(x)


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
    cases = build_cases(rng)
    lines = []
    for op, *rest in cases:
        lines.append(op + " " + " ".join(to_token(x) for x in rest))

    outs = run_driver(lines)
    if len(outs) != len(cases):
        print("line count mismatch: %d vs %d" % (len(outs), len(cases)))
        sys.exit(1)

    for case, out in zip(cases, outs):
        op = case[0]
        exp_ok, exp = expected(op, case[1:])
        want = ("R " if exp_ok else "E ") + exp
        if out != want:
            print("MISMATCH op=%s args=%r: got %r want %r"
                  % (op, case[1:], out, want))
            sys.exit(1)
    print("GOLDEN OK")


if __name__ == "__main__":
    main()
