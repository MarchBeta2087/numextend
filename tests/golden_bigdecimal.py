#!/usr/bin/env python3
"""bigdecimal 黄金对拍：随机生成测试用例，C 驱动 vs Python 精确参考。

参考实现用 fractions.Fraction 精确复刻设计文档 §8.1 / §7.3 的语义：
  - value = mant × 10^exp，mant 个位非 0（无尾随零，规范化）；
  - 指数范围 [emin, emax] = [−(10^exp_digits−1), 10^exp_digits−1]；
    上溢按舍入方向产生 ±∞ 或最大有限值，下溢 flush-to-zero；
  - 正确舍入（保护位语义）由 Fraction 精确运算保证；
  - to_str 精确输出（mant × 10^exp 直接展开，定点 / 科学计数）。

用法：
    python golden_bigdecimal.py [driver_path]

driver_path 默认为 ./golden_bigdecimal_driver；可通过环境变量
NEX_GOLDEN_DRIVER 覆盖。协议见 golden_bigdecimal_driver.c 头部注释。
全部通过打印 GOLDEN OK 并退出 0。
"""
import math
import os
import random
import subprocess
import sys
from fractions import Fraction

# 大指数用例（如 1e-9999）的中间整数可达上万位，放宽 Python 的
# int↔str 转换位数上限
if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)

DRIVER = os.environ.get(
    "NEX_GOLDEN_DRIVER",
    sys.argv[1] if len(sys.argv) > 1 else "./golden_bigdecimal_driver")

RAND_SEED = int(os.environ.get("NEX_GOLDEN_SEED", "20260812"))

# 舍入模式（与 bigdecimal_round_ty 一致）
R_NEAREST = 0
R_TOWARD_ZERO = 1
R_TOWARD_POS = 2
R_TOWARD_NEG = 3
R_AWAY_ZERO = 4

# 标志（与 bigdecimal_flag_ty 一致）
F_POS_ZERO, F_NEG_ZERO, F_POS, F_NEG, F_POS_INF, F_NEG_INF, F_NAN = range(7)


class Ctx:
    def __init__(self, mant_digits, exp_digits, mode):
        self.p = mant_digits
        self.emin = -(10 ** exp_digits - 1)
        self.exp_bits = exp_digits
        self.emax = 10 ** exp_digits - 1
        self.mode = mode


def floor_log10(fr):
    """fr > 0 的 floor(log10(fr))。"""
    e = len(str(fr.numerator)) - len(str(fr.denominator))
    while Fraction(10) ** e > fr:
        e -= 1
    while Fraction(10) ** (e + 1) <= fr:
        e += 1
    return e


def round_to_mant(fr, p, mode, neg=False, sticky=False):
    """正分数 fr → (mant, e)，mant 舍入到 p 位、∈ [10^(p−1), 10^p]。
    neg 为原值符号（定向模式需要）；sticky 表示真实值 = fr + ε（ε>0
    极小），仅影响恰在 1/2 处的平局。
    """
    e = floor_log10(fr) - (p - 1)
    scaled = fr * Fraction(10) ** (-e)
    q = scaled.numerator // scaled.denominator
    r = scaled - q
    if mode == R_NEAREST:
        # r > 1/2 → 进位；r == 1/2 → ties-to-even（sticky 视为 > 1/2）
        if r * 2 > 1:
            q += 1
        elif r * 2 == 1 and (sticky or (q & 1)):
            q += 1
    elif mode == R_TOWARD_POS:
        # 向 +∞：正值向上（含粘位）；负值向下（幅值取整仅当原值为正）
        if (r > 0 or sticky) and not neg:
            q += 1
    elif mode == R_TOWARD_NEG:
        # 向 −∞：负值幅值向上（含粘位）；正值向下
        if (r > 0 or sticky) and neg:
            q += 1
    elif mode == R_AWAY_ZERO:
        if r > 0 or sticky:
            q += 1
    if q == 10 ** p:
        # 进位到 10^p：规范化 10^p = 1 × 10^p
        q = 1
        e += p
    # 规范化：去尾随零
    while q % 10 == 0:
        q //= 10
        e += 1
    return q, e


def round_ctx(fr, ctx, sticky=False):
    """精确有理数 → (flag, mant, exp)，含范围检查与零处理（§8.1/§7.3）。"""
    if fr == 0:
        return (F_NEG_ZERO if ctx.mode == R_TOWARD_NEG else F_POS_ZERO, 0, 0)
    neg = fr < 0
    a = -fr if neg else fr
    q, e = round_to_mant(a, ctx.p, ctx.mode, neg=neg, sticky=sticky)
    if e > ctx.emax:
        to_inf = (ctx.mode in (R_NEAREST, R_AWAY_ZERO)
                  or (ctx.mode == R_TOWARD_POS and not neg)
                  or (ctx.mode == R_TOWARD_NEG and neg))
        if to_inf:
            return (F_NEG_INF if neg else F_POS_INF, 0, 0)
        return (F_NEG if neg else F_POS, 10 ** ctx.p - 1, ctx.emax)
    if e < ctx.emin:
        # 下溢 flush 零符号（IEEE §7.5）：向 +∞ 恒 +0，其余取精确结果符号
        if neg and ctx.mode != R_TOWARD_POS:
            return (F_NEG_ZERO, 0, 0)
        return (F_POS_ZERO, 0, 0)
    return (F_NEG if neg else F_POS, q, e)


def is_zero(f):
    return f in (F_POS_ZERO, F_NEG_ZERO)


def is_inf(f):
    return f in (F_POS_INF, F_NEG_INF)


def is_neg(f):
    return f in (F_NEG, F_NEG_ZERO, F_NEG_INF)


def value_of(f, mant, exp):
    v = Fraction(mant, 1) * Fraction(10) ** exp
    return -v if f == F_NEG else v


def add(a, b, ctx):
    fa, ma, ea = a
    fb, mb, eb = b
    if fa == F_NAN or fb == F_NAN:
        return (F_NAN, 0, 0)
    if is_inf(fa) or is_inf(fb):
        if is_inf(fa) and is_inf(fb) and fa != fb:
            return (F_NAN, 0, 0)  # +∞ + −∞
        return (fa if is_inf(fa) else fb, 0, 0)
    if is_zero(fa) and is_zero(fb):
        if fa == fb:
            return (fa, 0, 0)
        return (F_NEG_ZERO if ctx.mode == R_TOWARD_NEG else F_POS_ZERO, 0, 0)
    if is_zero(fa):
        return (fb, mb, eb)
    if is_zero(fb):
        return (fa, ma, ea)
    return round_ctx(value_of(*a) + value_of(*b), ctx)


def sub(a, b, ctx):
    fa, ma, ea = a
    fb, mb, eb = b
    if fa == F_NAN or fb == F_NAN:
        return (F_NAN, 0, 0)
    if is_inf(fa) or is_inf(fb):
        if is_inf(fa) and is_inf(fb) and fa == fb:
            return (F_NAN, 0, 0)  # ∞ − ∞
        if is_inf(fa):
            return (fa, 0, 0)
        return (F_NEG_INF if fb == F_POS_INF else F_POS_INF, 0, 0)
    if is_zero(fa) and is_zero(fb):
        if fa == fb:
            return (fa, 0, 0)
        return (F_NEG_ZERO if ctx.mode == R_TOWARD_NEG else F_POS_ZERO, 0, 0)
    if is_zero(fa):
        return neg((fb, mb, eb))  # 0 − x = −x
    if is_zero(fb):
        return (fa, ma, ea)
    return round_ctx(value_of(*a) - value_of(*b), ctx)


def mul(a, b, ctx):
    fa, ma, ea = a
    fb, mb, eb = b
    if fa == F_NAN or fb == F_NAN:
        return (F_NAN, 0, 0)
    neg = is_neg(fa) != is_neg(fb)
    if is_inf(fa) or is_inf(fb):
        if is_zero(fa) or is_zero(fb):
            return (F_NAN, 0, 0)  # 0 × ∞
        return (F_NEG_INF if neg else F_POS_INF, 0, 0)
    if is_zero(fa) or is_zero(fb):
        return (F_NEG_ZERO if neg else F_POS_ZERO, 0, 0)
    return round_ctx(value_of(*a) * value_of(*b), ctx)


def div(a, b, ctx):
    fa, ma, ea = a
    fb, mb, eb = b
    if fa == F_NAN or fb == F_NAN:
        return (F_NAN, 0, 0)
    neg = is_neg(fa) != is_neg(fb)
    if is_zero(fb):
        return (F_NAN, 0, 0) if is_zero(fa) else (
            F_NEG_INF if neg else F_POS_INF, 0, 0)
    if is_inf(fa) or is_inf(fb):
        if is_inf(fa) and is_inf(fb):
            return (F_NAN, 0, 0)  # ∞ / ∞
        if is_inf(fa):
            return (F_NEG_INF if neg else F_POS_INF, 0, 0)
        return (F_NEG_ZERO if neg else F_POS_ZERO, 0, 0)  # x / ∞
    if is_zero(fa):
        return (F_NEG_ZERO if neg else F_POS_ZERO, 0, 0)
    return round_ctx(value_of(*a) / value_of(*b), ctx)


def sqrt(a, ctx):
    """sqrt 正确舍入到 ctx（与 C 实现同构：尾数×10^指数 形式，
    保护位整数开方）。"""
    fa, ma, ea = a
    if fa == F_NAN or fa == F_NEG_INF:
        return (F_NAN, 0, 0)
    if fa == F_POS_INF:
        return (F_POS_INF, 0, 0)
    if is_zero(fa):
        return (fa, 0, 0)
    if fa == F_NEG:
        return (F_NAN, 0, 0)
    # value = ma × 10^ea；先把指数调整为偶数（Y × 10^(2k) 形式）
    if ea % 2 != 0:
        y = ma * 10
        e_adj = (ea - 1) // 2
    else:
        y = ma
        e_adj = ea // 2
    # 缩放 Y 使 sqrt 位长 = mant_digits + 2（含保护位）
    p = ctx.p
    target = 2 * (p + 2)
    dl = len(str(y))
    sticky = False
    if dl > target:
        r = dl - target if (dl - target) % 2 == 0 else dl - (target - 1)
        k = r // 2
        sticky = (y % (10 ** (2 * k))) != 0  # 右移丢位
        y //= 10 ** (2 * k)
        scale = k
    elif dl < target - 1:
        l = target - dl if (target - dl) % 2 == 0 else target - 1 - dl
        y *= 10 ** l
        scale = -(l // 2)
    else:
        scale = 0
    q = math.isqrt(y)
    sticky = sticky or (y - q * q > 0)
    val = Fraction(q) * Fraction(10) ** (e_adj + scale)
    return round_ctx(val, ctx, sticky=sticky)


def neg(a):
    fa, ma, ea = a
    if fa == F_NAN:
        return a
    flip = {F_POS_ZERO: F_NEG_ZERO, F_NEG_ZERO: F_POS_ZERO,
            F_POS: F_NEG, F_NEG: F_POS, F_POS_INF: F_NEG_INF,
            F_NEG_INF: F_POS_INF}
    return (flip[fa], ma, ea)


def cmp(a, b):
    fa, ma, ea = a
    fb, mb, eb = b
    if fa == F_NAN or fb == F_NAN:
        return 2
    rank = {F_NEG_INF: 0, F_NEG: 1, F_POS_ZERO: 2, F_NEG_ZERO: 2,
            F_POS: 3, F_POS_INF: 4}
    if rank[fa] != rank[fb]:
        return -1 if rank[fa] < rank[fb] else 1
    if rank[fa] in (0, 2, 4):
        return 0
    x = value_of(*a)
    y = value_of(*b)
    return (x > y) - (x < y)


def parse_decimal(s, ctx):
    """十进制串（含 inf/nan）→ (flag, mant, exp)，按 ctx 正确舍入。"""
    if s in ("inf", "+inf"):
        return (F_POS_INF, 0, 0)
    if s == "-inf":
        return (F_NEG_INF, 0, 0)
    if s in ("nan", "-nan", "+nan"):
        return (F_NAN, 0, 0)
    neg = s.startswith("-")
    t = s.lstrip("+-")
    if "e" in t or "E" in t:
        mant_s, exp_s = t.lower().split("e", 1)
        exp10 = int(exp_s)
    else:
        mant_s, exp10 = t, 0
    if "." in mant_s:
        ip, fp = mant_s.split(".", 1)
        digits = ip + fp
        exp10 -= len(fp)
    else:
        digits = mant_s
    if not digits or set(digits) == {"0"}:
        return (F_NEG_ZERO if neg else F_POS_ZERO, 0, 0)
    fr = Fraction(int(digits), 1) * Fraction(10) ** exp10
    return round_ctx(-fr if neg else fr, ctx)


def to_str_expected(f, mant, exp, fmt):
    """to_str 精确输出期望（与 C 实现同构：mant × 10^exp 展开）。"""
    if f == F_NAN:
        return "nan"
    if f == F_POS_ZERO:
        return "0"
    if f == F_NEG_ZERO:
        return "-0"
    if f == F_POS_INF:
        return "inf"
    if f == F_NEG_INF:
        return "-inf"
    ms = str(mant)
    dl = len(ms)
    neg = f == F_NEG
    if fmt == 1:  # 科学计数
        sci = exp + dl - 1
        s = ms[0] + ("." + ms[1:] if dl > 1 else "") + "e" \
            + ("+" if sci >= 0 else "-") + str(abs(sci))
    elif exp >= 0:
        s = ms + "0" * exp
    elif -exp >= dl:
        s = "0." + "0" * (-exp - dl) + ms
    else:
        cut = dl + exp
        s = ms[:cut] + "." + ms[cut:]
    return ("-" if neg else "") + s


# ----------------------------------------------------------------------

def rand_decimal(rng):
    nd = rng.randrange(1, 45)
    digits = str(rng.randrange(1, 10)) + "".join(
        str(rng.randrange(10)) for _ in range(nd - 1))
    exp10 = rng.randrange(-600, 601)
    if rng.randrange(3) == 0:
        return "%s.%se%d" % (digits[:1], digits[1:], exp10)
    return "%se%d" % (digits, exp10)


def rand_ctx(rng):
    return Ctx(rng.choice([7, 16, 34]), rng.choice([2, 3, 4]),
               rng.randrange(5))


def fmt_value(v):
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

    def emit(line, want):
        lines.append(line)
        expected.append(want)

    def set_ctx(ctx):
        emit("bd_ctx %d %d %d" % (ctx.p, ctx_eb(ctx), ctx.mode), "R ok")

    # 随机算术 / 解析 / 比较
    for _ in range(400):
        ctx = rand_ctx(rng)
        a = rand_decimal(rng)
        b = rand_decimal(rng)
        va = parse_decimal(a, ctx)
        vb = parse_decimal(b, ctx)
        set_ctx(ctx)
        emit("bd_parse %s" % a, "R " + fmt_value(va))
        emit("bd_parse %s" % b, "R " + fmt_value(vb))
        emit("bd_add %s %s" % (a, b), "R " + fmt_value(add(va, vb, ctx)))
        emit("bd_sub %s %s" % (a, b), "R " + fmt_value(sub(va, vb, ctx)))
        emit("bd_mul %s %s" % (a, b), "R " + fmt_value(mul(va, vb, ctx)))
        emit("bd_div %s %s" % (a, b), "R " + fmt_value(div(va, vb, ctx)))
        emit("bd_sqrt %s" % a, "R " + fmt_value(sqrt(va, ctx)))
        emit("bd_cmp %s %s" % (a, b), "R %d" % cmp(va, vb))

    # 特殊值与边界（全部五种舍入模式）
    fixed_ctxs = [Ctx(7, 2, R_NEAREST), Ctx(16, 3, R_NEAREST),
                  Ctx(34, 4, R_NEAREST), Ctx(7, 2, R_TOWARD_ZERO),
                  Ctx(16, 3, R_TOWARD_POS), Ctx(16, 3, R_TOWARD_NEG),
                  Ctx(34, 4, R_AWAY_ZERO)]
    for ctx in fixed_ctxs:
        set_ctx(ctx)
        for s in ["0", "-0", "1", "-1", "0.5", "1e600", "-1e-600",
                  "inf", "-inf", "nan", "3.14159265358979323846",
                  "1e-9999", "9.999999999999999", "2.5", "-2.5",
                  "12345678912345678912345678912345678912345678",
                  "1e100", "0.000000000000000001"]:
            emit("bd_parse %s" % s, "R " + fmt_value(parse_decimal(s, ctx)))

    # from_bigint（大整数 / 负 / 零）
    set_ctx(Ctx(16, 3, R_NEAREST))
    for s in ["0", "1", "-1", "123456789123456789123456789",
              "9999999999999999", "-12345", "10000000000000000"]:
        emit("bd_frombigint %s" % s,
             "R " + fmt_value(round_ctx(Fraction(int(s)), Ctx(16, 3, R_NEAREST))))

    # to_str 精确输出（定点 + 科学，两种格式都要验证）
    vctx = Ctx(16, 3, R_NEAREST)
    set_ctx(vctx)
    for s in ["0", "-0", "1", "-1", "0.1", "1e300", "-12.34", "12300",
              "0.00123", "3.141592653589793", "1e-300", "inf", "-inf",
              "nan", "1234567891234567", "100", "1.5", "0.5"]:
        v = parse_decimal(s, vctx)
        emit("bd_parse %s" % s, "R " + fmt_value(v))
        for fmt in (0, 1):
            emit("bd_tstr %d %s" % (fmt, fmt_value(v)),
                 "R " + to_str_expected(*v, fmt))

    # neg / comp / decomp 回环
    for s in ["1", "-1", "12.34", "0", "-0", "inf", "nan"]:
        v = parse_decimal(s, vctx)
        emit("bd_neg %s" % fmt_value(v), "R " + fmt_value(neg(v)))
        emit("bd_comp %s" % fmt_value(v), "R " + fmt_value(v))
        emit("bd_decomp %s" % fmt_value(v), "R " + fmt_value(v))

    outs = run_driver(lines)
    if len(outs) != len(expected):
        print("line count mismatch: %d vs %d" % (len(outs), len(expected)))
        sys.exit(1)
    fails = 0
    for line, out, want in zip(lines, outs, expected):
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


def ctx_eb(ctx):
    # emax = 10^exp_digits − 1 → exp_digits = len(str(emax))
    return len(str(ctx.emax))


if __name__ == "__main__":
    main()
