#!/usr/bin/env python3
"""路线 A 级数求值的 mpmath 黄金对拍（TODO §C4）。

用法：python3 golden_series.py <golden_series_driver>

对 exp/sin/cos/ln(1+z)/atan 在若干点与精度下，将驱动输出与 mpmath 的
高精度参考比较。mpmath 缺失时打印 SKIP 并以 0 退出（CI 可无 mpmath）。
"""

import subprocess
import sys

try:
    import mpmath
except ImportError:  # pragma: no cover
    print("SKIP: mpmath not installed")
    sys.exit(0)

FUNCS = {
    "exp": lambda mp, x: mp.exp(x),
    "sin": lambda mp, x: mp.sin(x),
    "cos": lambda mp, x: mp.cos(x),
    "ln1p": lambda mp, x: mp.log(1 + x),
    "atan": lambda mp, x: mp.atan(x),
    "atanpub": lambda mp, x: mp.atan(x),
    "atanpubdec": lambda mp, x: mp.atan(x),
    "exppub": lambda mp, x: mp.exp(x),
    "exppubdec": lambda mp, x: mp.exp(x),
    "logpub": lambda mp, x: mp.log(x),
    "logpubdec": lambda mp, x: mp.log(x),
    "sinpub": lambda mp, x: mp.sin(x),
    "cospub": lambda mp, x: mp.cos(x),
    "tanpub": lambda mp, x: mp.tan(x),
    "sinpubdec": lambda mp, x: mp.sin(x),
    "cospubdec": lambda mp, x: mp.cos(x),
    "tanpubdec": lambda mp, x: mp.tan(x),
    "asinpub": lambda mp, x: mp.asin(x),
    "acospub": lambda mp, x: mp.acos(x),
    "asinpubdec": lambda mp, x: mp.asin(x),
    "acospubdec": lambda mp, x: mp.acos(x),
}

CASES = [
    ("exp", 64, "0.5"),
    ("exp", 128, "0.25"),
    ("exp", 256, "-0.5"),
    ("sin", 64, "0.5"),
    ("sin", 128, "-0.5"),
    ("cos", 64, "0.5"),
    ("cos", 128, "0.25"),
    ("ln1p", 64, "0.5"),
    ("ln1p", 128, "0.25"),
    ("atan", 64, "0.5"),
    ("atan", 128, "0.25"),
    ("atanpub", 64, "1"),
    ("atanpub", 128, "2"),
    ("atanpub", 256, "-10"),
    # 十进制正确舍入（precision 为有效数字位数）
    ("atanpubdec", 16, "1"),
    ("atanpubdec", 16, "-0.5"),
    ("atanpubdec", 20, "2"),
    ("atanpubdec", 34, "-10"),
    ("exppub", 64, "0.5"),
    ("exppub", 128, "-1"),
    ("exppub", 256, "2"),
    ("exppubdec", 16, "1"),
    ("exppubdec", 16, "-0.5"),
    ("exppubdec", 20, "10"),
    ("logpub", 64, "2"),
    ("logpub", 128, "10"),
    ("logpub", 256, "0.5"),
    ("logpubdec", 16, "2"),
    ("logpubdec", 16, "1.5"),
    ("logpubdec", 20, "10"),
    ("sinpub", 64, "1"),
    ("cospub", 64, "1"),
    ("tanpub", 64, "0.5"),
    ("sinpub", 128, "-2"),
    ("cospub", 256, "3"),
    ("sinpubdec", 16, "1"),
    ("cospubdec", 16, "1"),
    ("tanpubdec", 16, "0.5"),
    ("sinpubdec", 20, "2"),
    ("asinpub", 64, "0.5"),
    ("acospub", 64, "0.5"),
    ("asinpub", 128, "-1"),
    ("acospub", 256, "-0.25"),
    ("asinpubdec", 16, "0.5"),
    ("acospubdec", 16, "0.5"),
    ("asinpubdec", 20, "-1"),
    # Payne–Hanek：超大实参（float 侧取 2 的幂，可被二进制精度精确表示）
    ("sinpub", 64, "1267650600228229401496703205376"),
    ("cospub", 64, "1267650600228229401496703205376"),
    ("sinpub", 64, "-1267650600228229401496703205376"),
    ("tanpub", 64, "1267650600228229401496703205376"),
    ("sinpub", 128, "1606938044258990275541962092341162602522202993782792835301376"),
    ("cospub", 128, "1606938044258990275541962092341162602522202993782792835301376"),
    ("sinpub", 256, "1267650600228229401496703205376"),
    # 十进制超大实参（精确的 10 的幂）
    ("sinpubdec", 16, "1e30"),
    ("cospubdec", 16, "1e30"),
    ("sinpubdec", 16, "1e100"),
    ("cospubdec", 16, "1e100"),
    ("sinpubdec", 20, "-1e1000"),
    ("cospubdec", 20, "1e1000"),
    ("sinpubdec", 34, "1e100"),
]


def main():
    driver = sys.argv[1]
    stdin = "".join("%s %d %s\n" % c for c in CASES)
    proc = subprocess.run([driver], input=stdin, capture_output=True,
                          text=True, check=True)
    lines = proc.stdout.splitlines()
    if len(lines) != len(CASES):
        print("FAIL: driver produced %d lines, expected %d"
              % (len(lines), len(CASES)))
        return 1

    failures = 0
    for (fn, bits, z), line in zip(CASES, lines):
        if line == "ERR":
            print("FAIL: %s %d %s -> ERR" % (fn, bits, z))
            failures += 1
            continue
        # 实参量级：|x| ≈ 10^(digits + exp)，十进制位数 × 4 是二进制位数的上界
        body = z.lower()
        exp10 = 0
        if "e" in body:
            body, exp_part = body.split("e")
            exp10 = int(exp_part)
        arg_bits = (sum(ch.isdigit() for ch in body) + abs(exp10)) * 4
        if fn.endswith("dec"):
            mpmath.mp.prec = bits * 4 + 40 + arg_bits
            ref = FUNCS[fn](mpmath.mp, mpmath.mpf(z))
            got = mpmath.mpf(line)
            tol = mpmath.mpf(10) ** (-(bits - 2))
        else:
            mpmath.mp.prec = bits + 20 + arg_bits
            ref = FUNCS[fn](mpmath.mp, mpmath.mpf(z))
            got = mpmath.mpf(line)
            tol = mpmath.mpf(2) ** (-(bits - 4))
        if abs(got - ref) > tol * max(mpmath.mpf(1), abs(ref)):
            print("FAIL: %s %d %s got=%s ref=%s" % (fn, bits, z, got, ref))
            failures += 1

    print("golden_series: %s" % ("OK" if failures == 0 else
                                 "%d FAIL" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
