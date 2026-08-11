#!/usr/bin/env python3
"""bigfloat 黄金对拍：随机生成测试用例，C 驱动 vs Python 精确参考。

参考实现用 fractions.Fraction 精确复刻设计文档 §7.3 的语义：
  - value = mant × 2^exp，mant 恰为 mant_bits 位（最高位恒 1）；
  - 指数范围 [emin, emax] = [−2^(exp_bits−1), 2^(exp_bits−1)−1]；
    上溢按舍入方向产生 ±∞ 或最大有限值，下溢 flush-to-zero；
  - 正确舍入（含保护位语义）由 Fraction 精确运算保证。

用法：
    python golden_bigfloat.py [driver_path]

driver_path 默认为 ./golden_bigfloat_driver；可通过环境变量
NEX_GOLDEN_DRIVER 覆盖。协议见 golden_bigfloat_driver.c 头部注释。
全部通过打印 GOLDEN OK 并退出 0。
"""
import math
import os
import random
import subprocess
import sys
from fractions import Fraction

DRIVER = os.environ.get(
    "NEX_GOLDEN_DRIVER",
    sys.argv[1] if len(sys.argv) > 1 else "./golden_bigfloat_driver")

RAND_SEED = 20260812

# 舍入模式（与 bigfloat_round_ty 一致）
R_NEAREST = 0
R_TOWARD_ZERO = 1
R_TOWARD_POS = 2
R_TOWARD_NEG = 3
R_AWAY_ZERO = 4

# 标志（与 bigfloat_flag_ty 一致）
F_POS_ZERO, F_NEG_ZERO, F_POS, F_NEG, F_POS_INF, F_NEG_INF, F_NAN = range(7)


class Ctx:
    def __init__(self, mant_bits, exp_bits, mode):
        self.p = mant_bits
        self.emin = -(1 << (exp_bits - 1))
        self.emax = (1 << (exp_bits - 1)) - 1
        self.mode = mode


def floor_log2(fr):
    """fr > 0 的 floor(log2(fr))。"""
    e = fr.numerator.bit_length() - fr.denominator.bit_length()
    while Fraction(2) ** e > fr:
        e -= 1
    while Fraction(2) ** (e + 1) <= fr:
        e += 1
    return e


def round_frac(fr):
    """正分数最近偶舍入为整数。"""
    q = fr.numerator // fr.denominator
    r = fr - q
    if r * 2 > fr.denominator:
        q += 1
    elif r * 2 == fr.denominator and (q & 1):
        q += 1
    return q


def round_to_mant(fr, p, mode, neg=False, sticky=False):
    """正分数 fr → (mant, e)，mant 舍入到 p 位、∈ [2^(p−1), 2^p]。
    neg 为原值符号（定向模式需要）；sticky 表示真实值 = fr + ε（ε>0
    极小），仅影响恰在 1/2 处的平局。
    """
    e = floor_log2(fr) - (p - 1)
    scaled = fr * Fraction(2) ** (-e)
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
    if q == (1 << p):
        q >>= 1
        e += 1
    return q, e


def round_ctx(fr, ctx, sticky=False):
    """精确有理数 → (flag, mant, exp)，含范围检查与零处理（§7.3）。"""
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
        return (F_NEG if neg else F_POS, (1 << ctx.p) - 1, ctx.emax)
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
    v = Fraction(mant, 1) * Fraction(2) ** exp
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


def sqrt_round(fr, ctx):
    """sqrt(正分数) 正确舍入到 ctx（保护位整数开方，与 C 实现同构）。"""
    p = ctx.p
    a, b = fr.numerator, fr.denominator
    n0 = a * b  # sqrt(fr) = sqrt(n0) / b
    target = 2 * (p + 2)
    bl = n0.bit_length()
    sticky = False
    if bl > target:
        r = bl - target if (bl - target) % 2 == 0 else bl - (target - 1)
        k = r // 2
        sticky = (n0 & ((1 << (2 * k)) - 1)) != 0  # 右移丢位
        n0 >>= 2 * k
        scale = k
    elif bl < target - 1:
        l = target - bl if (target - bl) % 2 == 0 else target - 1 - bl
        n0 <<= l
        scale = -(l // 2)
    else:
        scale = 0
    q = math.isqrt(n0)
    sticky = sticky or (n0 - q * q > 0)
    val = Fraction(q, b) * Fraction(2) ** scale
    return round_ctx(val, ctx, sticky=sticky)


def sqrt(a, ctx):
    fa, ma, ea = a
    if fa == F_NAN or fa == F_NEG_INF:
        return (F_NAN, 0, 0)
    if fa == F_POS_INF:
        return (F_POS_INF, 0, 0)
    if is_zero(fa):
        return (fa, 0, 0)
    if fa == F_NEG:
        return (F_NAN, 0, 0)
    return sqrt_round(value_of(*a), ctx)


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


def f64_to_value(bits):
    """double 位模式 → (flag, mant, exp)，精确（复刻 from_f64）。"""
    neg = (bits >> 63) & 1
    expf = (bits >> 52) & 0x7FF
    frac = bits & 0xFFFFFFFFFFFFF
    if expf == 0x7FF:
        return (F_NAN if frac else (F_NEG_INF if neg else F_POS_INF), 0, 0)
    if expf == 0 and frac == 0:
        return (F_NEG_ZERO if neg else F_POS_ZERO, 0, 0)
    if expf == 0:
        bl = frac.bit_length()
        lshift = 53 - bl
        return (F_NEG if neg else F_POS, frac << lshift, -1074 - lshift)
    return (F_NEG if neg else F_POS, (1 << 52) | frac, expf - 1075)


def value_to_f64(f, mant, exp):
    """(flag, mant, exp) → (double 位模式, 溢出标志)，复刻 to_f64。"""
    if f == F_NAN:
        return 0x7FF8000000000000, 0
    if f == F_POS_INF:
        return 0x7FF0000000000000, 0
    if f == F_NEG_INF:
        return 0xFFF0000000000000, 0
    if f in (F_POS_ZERO, F_NEG_ZERO):
        return (0x8000000000000000 if f == F_NEG_ZERO else 0), 0
    neg = f == F_NEG
    v = value_of(f, mant, exp)
    a = -v if neg else v
    sign = 0x8000000000000000 if neg else 0
    if a >= Fraction(2) ** 1024:
        return (sign | 0x7FF0000000000000), 1
    if a < Fraction(2) ** -1074:
        return sign, 0
    if a >= Fraction(2) ** -1022:
        e = floor_log2(a)
        mant53 = round_frac(a * Fraction(2) ** (52 - e))
        if mant53 == 1 << 53:
            e += 1
            mant53 >>= 1
        return sign | ((e + 1023) << 52) | (mant53 - (1 << 52)), 0
    mant52 = round_frac(a * Fraction(2) ** 1074)
    if mant52 == 1 << 52:
        return sign | (1 << 52), 0  # 进位到最小正常数
    return sign | mant52, 0


def to_str_check(f, mant, exp, s):
    """验证 to_str 输出 s：按最近偶、同精度解析回等于原值。"""
    if f == F_NAN:
        return s in ("nan", "-nan")
    p = mant.bit_length()
    vctx = Ctx(max(p, 2), 61, R_NEAREST)
    return parse_decimal(s, vctx) == (f, mant, exp)


# ----------------------------------------------------------------------

def rand_decimal(rng):
    nd = rng.randrange(1, 40)
    digits = str(rng.randrange(1, 10)) + "".join(
        str(rng.randrange(10)) for _ in range(nd - 1))
    exp10 = rng.randrange(-400, 401)
    if rng.randrange(3) == 0:
        return "%s.%se%d" % (digits[:1], digits[1:], exp10)
    return "%se%d" % (digits, exp10)


def rand_ctx(rng):
    return Ctx(rng.choice([24, 53, 113]), rng.choice([8, 11, 15]),
               rng.randrange(5))


def fmt_value(v):
    return "%d %d %d" % v


def report(op, args, out, expected):
    want = "R " + fmt_value(expected) if isinstance(expected, tuple) \
        else "R %s" % expected
    print("MISMATCH op=%s args=%r: got %r want %r" % (op, args, out, want))


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
        emit("bf_ctx %d %d %d" % (ctx.p, ctx_eb(ctx), ctx.mode), "R ok")

    # 随机算术 / 解析 / 比较
    for _ in range(400):
        ctx = rand_ctx(rng)
        a = rand_decimal(rng)
        b = rand_decimal(rng)
        va = parse_decimal(a, ctx)
        vb = parse_decimal(b, ctx)
        set_ctx(ctx)
        emit("bf_parse %s" % a, "R " + fmt_value(va))
        emit("bf_parse %s" % b, "R " + fmt_value(vb))
        emit("bf_add %s %s" % (a, b), "R " + fmt_value(add(va, vb, ctx)))
        emit("bf_sub %s %s" % (a, b), "R " + fmt_value(sub(va, vb, ctx)))
        emit("bf_mul %s %s" % (a, b), "R " + fmt_value(mul(va, vb, ctx)))
        emit("bf_div %s %s" % (a, b), "R " + fmt_value(div(va, vb, ctx)))
        emit("bf_sqrt %s" % a, "R " + fmt_value(sqrt(va, ctx)))
        emit("bf_cmp %s %s" % (a, b), "R %d" % cmp(va, vb))

    # 特殊值与边界（全部五种舍入模式）
    fixed_ctxs = [Ctx(24, 8, R_NEAREST), Ctx(53, 11, R_NEAREST),
                  Ctx(113, 15, R_NEAREST), Ctx(24, 8, R_TOWARD_ZERO),
                  Ctx(53, 11, R_TOWARD_POS), Ctx(53, 11, R_TOWARD_NEG),
                  Ctx(113, 15, R_AWAY_ZERO)]
    for ctx in fixed_ctxs:
        set_ctx(ctx)
        for s in ["0", "-0", "1", "-1", "0.5", "1e400", "-1e-400",
                  "inf", "-inf", "nan", "3.14159265358979323846",
                  "1e-45", "1.7976931348623157e308", "5e-324", "2.5",
                  "-2.5", "1e100", "1.0000000000000002"]:
            emit("bf_parse %s" % s, "R " + fmt_value(parse_decimal(s, ctx)))

    # f64 位模式往返
    for pat in ["0000000000000000", "8000000000000000",
                "3ff0000000000000", "bff0000000000000",
                "3fe0000000000000", "400921fb54442d18",
                "7fefffffffffffff", "0010000000000000",
                "0000000000000001", "7ff0000000000000",
                "fff0000000000000", "7ff8000000000000",
                "3e45798ee2308c3a", "3b70000000000000",
                "c3b7000000000000"]:
        emit("bf_f64 %s" % pat,
             "R " + fmt_value(f64_to_value(int(pat, 16))))

    # to_str 回环（binary64 下解析出的值）
    vctx = Ctx(53, 11, R_NEAREST)
    set_ctx(vctx)
    for s in ["1", "-1", "0.1", "1e300", "-12.34", "3.141592653589793",
              "1.7976931348623157e308", "1e-300", "inf", "-inf", "nan",
              "123456789.123456789", "1e-324"]:
        v = parse_decimal(s, vctx)
        emit("bf_parse %s" % s, "R " + fmt_value(v))
        emit("bf_tstr %s" % fmt_value(v), expected_tstr(v))

    outs = run_driver(lines)
    if len(outs) != len(expected):
        print("line count mismatch: %d vs %d" % (len(outs), len(expected)))
        sys.exit(1)
    fails = 0
    for line, out, want in zip(lines, outs, expected):
        if line.startswith("bf_tstr") and want is None:
            # 正常值：回环验证——驱动输出串按最近偶、同精度解析回等于原值
            f, mant, exp = (int(x) for x in line.split()[1:])
            if out.startswith("R ") and to_str_check(f, mant, exp, out[2:]):
                continue
            print("MISMATCH(roundtrip): %s: got %r" % (line, out))
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


def expected_tstr(v):
    """to_str 期望：先由驱动生成串（无法在 Python 侧精确预测最短串），
    输出为占位，在验证阶段以回环检查替换。此处直接标记为待检查。"""
    f, mant, exp = v
    if f == F_NAN:
        return "R nan"
    if f == F_POS_ZERO:
        return "R 0"
    if f == F_NEG_ZERO:
        return "R -0"
    if f == F_POS_INF:
        return "R inf"
    if f == F_NEG_INF:
        return "R -inf"
    return None  # 正常值：驱动输出后回环验证


def ctx_eb(ctx):
    # emin = −2^(exp_bits−1) → exp_bits = abs(emin).bit_length()
    return abs(ctx.emin).bit_length()


if __name__ == "__main__":
    main()
