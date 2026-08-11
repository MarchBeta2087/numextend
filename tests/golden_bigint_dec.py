#!/usr/bin/env python3
"""bigint_dec 黄金对拍 + bin↔dec 互转交叉验证。

用法：
    python golden_bigint_dec.py [driver_path]

driver_path 默认为 ./golden_driver；可通过环境变量 NEX_GOLDEN_DRIVER 覆盖。
协议见 golden_driver.c 头部注释（d_ 前缀操作 bigint_dec，c_ 前缀走 conv 互转）。
全部通过打印 GOLDEN OK 并退出 0。
"""
import os
import random
import subprocess
import sys

# Python 3.11+ 默认将 int 转十进制字符串的长度限制在 4300 位以内，
# 这里解除限制以支持 pow 等用例中的大数输出（兼容 3.10 及更早版本）。
try:
    sys.set_int_max_str_digits(0)
except AttributeError:
    pass

DRIVER = os.environ.get("NEX_GOLDEN_DRIVER",
                        sys.argv[1] if len(sys.argv) > 1 else "./golden_driver")

RAND_SEED = 20260811
COUNT = 2000


def rand_big(rng, digits):
    """生成 digit 位十进制随机大整数（含前导符号）。"""
    if digits <= 0:
        return 0
    s = "".join(str(rng.randrange(10)) for _ in range(digits))
    s = s.lstrip("0") or "0"
    if s != "0" and rng.randrange(2):
        s = "-" + s
    return int(s)


def rand_special(rng):
    choice = rng.randrange(13)
    if choice == 0:
        return 0
    if choice == 1:
        return rng.choice([1, -1])
    if choice == 2:
        return rng.choice([2, -2, 3, -3, 7, -7, 10, -10])
    if choice == 3:
        # 十进制肢边界：10^9-1 与 10^9 的倍数邻域
        k = rng.choice([1, 2, 3])
        return rng.choice([10**(9 * k) - 1, 10**(9 * k),
                           -(10**(9 * k) - 1), -(10**(9 * k))])
    if choice == 4:
        return rng.choice([2**64 - 1, 2**64, -(2**64 - 1), -(2**64)])
    if choice == 5:
        return rng.choice([10**k for k in range(1, 40)])
    if choice == 6:
        return rand_big(rng, rng.choice([1, 2, 3, 5]))
    if choice == 7:
        return rand_big(rng, rng.choice([8, 9, 10, 18]))
    if choice == 8:
        return rand_big(rng, rng.choice([24, 30, 40]))
    if choice == 9:
        return rand_big(rng, rng.choice([50, 60, 80]))
    if choice == 10:
        return rand_big(rng, rng.choice([100, 120]))
    if choice == 11:
        return rand_big(rng, rng.choice([200, 300]))
    return rand_big(rng, rng.choice([500, 800]))


def c_div_trunc(a, b):
    """截断除法：q = trunc(a/b)，r 与 a 同号。"""
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    r = a - q * b
    return q, r


def trunc_div_pow10(a, n):
    """截断除以 10^n（向零取整）。"""
    p = 10 ** n
    q = abs(a) // p
    return -q if a < 0 else q


def build_cases(rng):
    cases = []
    # d_add / d_sub / d_mul / d_cmp：两个随机数
    for _ in range(COUNT):
        a = rand_special(rng)
        b = rand_special(rng)
        for op in ("d_add", "d_sub", "d_mul"):
            cases.append((op, a, b))
        cases.append(("d_cmp", a, b))
    # d_div：除数非零
    for _ in range(COUNT):
        a = rand_special(rng)
        b = rand_special(rng)
        if b == 0:
            b = 1
        cases.append(("d_div", a, b))
    # d_pow：指数 0..20（含 0^0）
    for _ in range(COUNT // 2):
        a = rand_special(rng)
        e = rng.randrange(21)
        cases.append(("d_pow", a, e))
    # d_pow_mod：m 为正
    for _ in range(COUNT // 2):
        b = rand_special(rng)
        e = rng.randrange(60)
        m = rand_special(rng)
        if m < 0:
            m = -m
        if m == 0:
            m = 1
        cases.append(("d_pow_mod", b, e, m))
    # d_mul_pow10 / d_div_pow10：移位 0..40 位（跨肢与肢内余数都覆盖）
    for _ in range(COUNT // 2):
        a = rand_special(rng)
        n = rng.randrange(0, 41)
        cases.append(("d_mul_pow10", a, n))
        cases.append(("d_div_pow10", a, n))
    # d_id：十进制字符串解析-输出往返
    for _ in range(COUNT // 4):
        cases.append(("d_id", rand_special(rng)))
    # 交叉验证：c_b2d / c_d2b 互转后十进制输出须与原值一致
    for _ in range(COUNT // 2):
        a = rand_special(rng)
        cases.append(("c_b2d", a))
        cases.append(("c_d2b", a))
    return cases


def expected(op, args):
    """返回 (ok, 结果字符串) 或 (False, 错误标签)。"""
    if op in ("d_add", "d_sub", "d_mul"):
        a, b = args
        r = a + b if op == "d_add" else (a - b if op == "d_sub" else a * b)
        return True, str(r)
    if op == "d_cmp":
        a, b = args
        r = 0 if a == b else (1 if a > b else -1)
        return True, str(r)
    if op == "d_div":
        a, b = args
        q, r = c_div_trunc(a, b)
        return True, "%s %s" % (q, r)
    if op == "d_pow":
        a, e = args
        return True, str(a ** e)
    if op == "d_pow_mod":
        b, e, m = args
        if m == 0:
            return False, "divzero"
        if e < 0 or m < 0:
            return False, "invalid"
        return True, str(pow(b, e, m))
    if op == "d_mul_pow10":
        a, n = args
        return True, str(a * 10 ** n)
    if op == "d_div_pow10":
        a, n = args
        return True, str(trunc_div_pow10(a, n))
    if op in ("d_id", "c_b2d", "c_d2b"):
        return True, str(args[0])
    raise AssertionError("unknown op " + op)


def run_driver(lines):
    proc = subprocess.run([DRIVER], input="\n".join(lines) + "\n",
                          capture_output=True, text=True)
    out = proc.stdout.splitlines()
    err = proc.stderr
    if proc.returncode != 0:
        return None, "driver exited %d stderr=%s" % (proc.returncode, err.strip())
    return out, None


def main():
    rng = random.Random(RAND_SEED)
    cases = build_cases(rng)

    lines = []
    for c in cases:
        op = c[0]
        args = c[1:]
        lines.append(op + " " + " ".join(str(x) for x in args))

    outs, drv_err = run_driver(lines)
    if drv_err is not None:
        print("FAIL cannot run driver:", drv_err)
        return 1
    if outs is None or len(outs) != len(cases):
        print("FAIL output line count %s != cases %d"
              % ("N/A" if outs is None else len(outs), len(cases)))
        return 1

    mismatches = 0
    for line, c in zip(outs, cases):
        op = c[0]
        args = c[1:]
        exp_ok, exp = expected(op, args)
        # 解析驱动输出
        if line.startswith("R "):
            got_ok, got = True, line[2:]
        elif line.startswith("E "):
            got_ok, got = False, line[2:]
        else:
            got_ok, got = False, "unexpected output"

        if exp_ok != got_ok or (exp_ok and exp != got):
            mismatches += 1
            if mismatches <= 10:
                print("MISMATCH op=%s args=%s" % (op, args))
                print("  expected: R %s" % exp if exp_ok else "  expected: E %s" % exp)
                print("  got     : %s" % line)

    if mismatches == 0:
        print("GOLDEN OK (%d cases)" % len(cases))
        return 0
    print("%d MISMATCHES" % mismatches)
    return 1


if __name__ == "__main__":
    sys.exit(main())
