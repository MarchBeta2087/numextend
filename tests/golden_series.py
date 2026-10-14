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
        mpmath.mp.prec = bits + 20
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
