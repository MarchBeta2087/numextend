#!/usr/bin/env python3
"""bigint_bin 黄金对拍：随机生成测试用例，C 驱动 vs Python 原生大整数。

用法：
    python golden_bigint_bin.py [driver_path]

driver_path 默认为 ./golden_driver；可通过环境变量 NEX_GOLDEN_DRIVER 覆盖。
协议见 golden_driver.c 头部注释。全部通过打印 GOLDEN OK 并退出 0。
"""
import os
import random
import subprocess
import sys

# CI/控制台编码容错：非 ASCII 输出以占位符代替，避免 cp1252 等
# 编码环境下 UnicodeEncodeError 崩溃
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")

# Python 3.11+ 默认将 int 转十进制字符串的长度限制在 4300 位以内，
# 这里解除限制以支持 pow 等用例中的大数输出（兼容 3.10 及更早版本）。
try:
    sys.set_int_max_str_digits(0)
except AttributeError:
    pass

DRIVER = os.environ.get("NEX_GOLDEN_DRIVER",
                        sys.argv[1] if len(sys.argv) > 1 else "./golden_driver")

RAND_SEED = 20260810
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
    choice = rng.randrange(12)
    if choice == 0:
        return 0
    if choice == 1:
        return rng.choice([1, -1])
    if choice == 2:
        return rng.choice([2, -2, 3, -3, 7, -7, 10, -10])
    if choice == 3:
        return rng.choice([2**64 - 1, 2**64, -(2**64 - 1), -(2**64)])
    if choice == 4:
        return rng.choice([10**k for k in range(1, 40)])
    if choice == 5:
        return rand_big(rng, rng.choice([1, 2, 3, 5]))
    if choice == 6:
        return rand_big(rng, rng.choice([8, 12, 18]))
    if choice == 7:
        return rand_big(rng, rng.choice([24, 30, 40]))
    if choice == 8:
        return rand_big(rng, rng.choice([50, 60, 80]))
    if choice == 9:
        return rand_big(rng, rng.choice([100, 120]))
    if choice == 10:
        return rand_big(rng, rng.choice([200, 300]))
    return rand_big(rng, rng.choice([500, 800]))


def c_div_trunc(a, b):
    """截断除法：q = trunc(a/b)，r 与 a 同号。"""
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    r = a - q * b
    return q, r


def build_cases(rng):
    cases = []
    # add / sub / mul / and / or / xor / cmp：两个随机数
    for _ in range(COUNT):
        a = rand_special(rng)
        b = rand_special(rng)
        for op in ("add", "sub", "mul", "and", "or", "xor"):
            cases.append((op, a, b))
        cases.append(("cmp", a, b))
    # mul_ntt / mul_fft：强制多模数 NTT 与浮点 FFT（设计文档 §4.3）。
    # 中小尺寸覆盖强制路径（N ≤ 2^10），大尺寸触发 N=2^13 变换；
    # 另含零/一/符号边界
    for _ in range(COUNT // 8):
        a = rand_big(rng, rng.choice([500, 800, 1500, 3000, 4000]))
        b = rand_big(rng, rng.choice([500, 800, 1500, 3000, 4000]))
        cases.append(("mul_ntt", a, b))
        cases.append(("mul_fft", a, b))
    for d1, d2 in [(12000, 12000), (20000, 16000), (20000, 20000)]:
        cases.append(("mul_ntt", rand_big(rng, d1), rand_big(rng, d2)))
        cases.append(("mul_fft", rand_big(rng, d1), rand_big(rng, d2)))
    for a, b in [(0, 0), (1, 1), (-1, 1), (1, -1), (-1, -1),
                 (0, 12345), (12345, 0), (10**30, 10**30)]:
        cases.append(("mul_ntt", a, b))
        cases.append(("mul_fft", a, b))
    # div：除数非零
    for _ in range(COUNT):
        a = rand_special(rng)
        b = rand_special(rng)
        if b == 0:
            b = 1
        cases.append(("div", a, b))
    # pow：指数 0..20（含 0^0）
    for _ in range(COUNT // 2):
        a = rand_special(rng)
        e = rng.randrange(21)
        cases.append(("pow", a, e))
    # pow_mod：m 为正
    for _ in range(COUNT // 2):
        b = rand_special(rng)
        e = rng.randrange(60)
        m = rand_special(rng)
        if m < 0:
            m = -m
        if m == 0:
            m = 1
        cases.append(("pow_mod", b, e, m))
    # shl / shr
    for _ in range(COUNT // 2):
        a = rand_special(rng)
        n = rng.randrange(0, 400)
        cases.append(("shl", a, n))
        cases.append(("shr", a, n))
    # c_b2d / c_d2b：bin↔dec 互转（设计文档 §4.2.6）。大数触发分治路径
    # （dec > 2048 肢 ≈ 18432 位、bin > 256 肢 ≈ 617 位），另含 10^k
    # 邻域（dec 肢基 10^9 边界）与零/一/符号
    for _ in range(COUNT // 8):
        v = rand_big(rng, rng.choice([600, 2000, 5000]))
        cases.append(("c_b2d", v))
        cases.append(("c_d2b", v))
    for _ in range(12):
        v = rand_big(rng, 20000)
        cases.append(("c_b2d", v))
        cases.append(("c_d2b", v))
    for v in [0, 1, -1, 10**600, 2**2000, -(10**800) + 7]:
        cases.append(("c_b2d", v))
        cases.append(("c_d2b", v))
    for k in [9, 18, 27, 36, 99, 999, 9999]:
        for v in [10**k - 1, 10**k, 10**k + 1]:
            cases.append(("c_b2d", v))
            cases.append(("c_d2b", v))
    for v in [10**19999 - 1, 10**19999 + 1, 2**10000]:
        cases.append(("c_b2d", v))
        cases.append(("c_d2b", v))
    return cases


def expected(op, args):
    """返回 (ok, 结果字符串) 或 (False, 错误标签)。"""
    if op in ("add", "sub", "mul", "mul_ntt", "mul_fft", "and", "or",
              "xor"):
        a, b = args
        if op == "add":
            r = a + b
        elif op == "sub":
            r = a - b
        elif op in ("mul", "mul_ntt", "mul_fft"):
            r = a * b
        elif op == "and":
            r = a & b
        elif op == "or":
            r = a | b
        else:
            r = a ^ b
        return True, str(r)
    if op == "c_b2d" or op == "c_d2b":
        # 互转往返：c_b2d 输出十进制、c_d2b 输出二进制（均为源值的精确表示）
        (a,) = args
        return True, str(a)
    if op == "cmp":
        a, b = args
        r = 0 if a == b else (1 if a > b else -1)
        return True, str(r)
    if op == "div":
        a, b = args
        q, r = c_div_trunc(a, b)
        return True, "%s %s" % (q, r)
    if op == "pow":
        a, e = args
        return True, str(a ** e)
    if op == "pow_mod":
        b, e, m = args
        if m == 0:
            return False, "divzero"
        if e < 0 or m < 0:
            return False, "invalid"
        return True, str(pow(b, e, m))
    if op in ("shl", "shr"):
        a, n = args
        if op == "shl":
            return True, str(a << n)
        # 负数右移为 floor，与 C 一致
        return True, str(a >> n)
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

    # 分块处理：驱动崩溃时精确定位到出错用例（块内逐条重跑）
    CHUNK = 100
    outs = []
    for start in range(0, len(lines), CHUNK):
        chunk = lines[start:start + CHUNK]
        chunk_out, drv_err = run_driver(chunk)
        bad = drv_err is not None
        if not bad and len(chunk_out) != len(chunk):
            bad = True
        if bad:
            # 块内崩溃：逐条重跑定位首个出错用例
            for i, line in enumerate(chunk):
                one, e1 = run_driver([line])
                if e1 is not None:
                    print("FAIL case %d (cmd: %s...) driver: %s"
                          % (start + i, line[:60], e1))
                    return 1
            print("FAIL chunk %d: crash not reproducible case-by-case"
                  % (start // CHUNK))
            return 1
        outs.extend(chunk_out)

    if len(outs) != len(cases):
        print("FAIL output line count %s != cases %d"
              % (len(outs), len(cases)))
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