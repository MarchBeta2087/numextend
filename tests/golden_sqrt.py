#!/usr/bin/env python3
"""sqrt 独立黄金对拍：与现有 golden 的"同构复刻"不同，本参考用**精确边界
比较法**（整数平方与边界²的精确比较）独立判定 sqrt 的正确舍入，覆盖全部
五种舍入模式——弥补"黄金参考与 C 用相同保护位算法"的独立性盲区。

方法（对 value = m × 2^e 或 m × 10^e）：
  - 结果指数 e_r = floor_log2/10(sqrt(value)) − (p−1)；
  - S = value × base^(−2·e_r)，sqrt(S) ∈ [base^(p−1), base^p)；
  - 按模式舍入 sqrt(S)：
      floor / toward-zero / toward-neg（正值）→ floor(sqrt(S))；
      ceil / away / toward-pos（正值）→ ceil(sqrt(S))；
      nearest → 最近偶（ties 比较 S 与 (q+0.5)²，精确整数）。
  - floor/ceil 经整数 isqrt 与精确平方比较（不依赖浮点）。
结果与 C 驱动（bigfloat_golden_driver / bigdecimal_golden_driver 的
bf_sqrt / bd_sqrt，含 ctx 模式）逐一对拍。

用法：python golden_sqrt.py [bf_driver] [bd_driver]
全部通过打印 GOLDEN OK 并退出 0；种子可经 NEX_GOLDEN_SEED 配置。
"""
import os
import random
import subprocess
import sys
from math import isqrt
from fractions import Fraction

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

RAND_SEED = int(os.environ.get("NEX_GOLDEN_SEED", "20260812"))
COUNT = int(os.environ.get("NEX_GOLDEN_SQRT_COUNT", "300"))

F_POS_ZERO, F_NEG_ZERO, F_POS, F_NEG, F_POS_INF, F_NEG_INF, F_NAN = range(7)
R_NEAREST, R_TZ, R_TP, R_TN, R_AWAY = range(5)


def is_zero(v):
    return v[0] in (F_POS_ZERO, F_NEG_ZERO)


def is_special(v):
    return v[0] in (F_POS_INF, F_NEG_INF, F_NAN)


def value_fraction(v, base):
    """(flag, mant, exp) → 精确 Fraction。特殊值返回 None。"""
    if is_special(v):
        return None
    f, mant, exp = v
    fr = Fraction(mant) * Fraction(base) ** exp
    return -fr if f == F_NEG else fr


def floor_log(fr, base):
    """fr > 0 的 floor(log_base(fr))，整数精确。"""
    e = 0
    b = Fraction(base)
    while b ** (e + 1) <= fr:
        e += 1
    while b ** e > fr:
        e -= 1
    return e


def sqrt_boundary_exact(fr, p, mode, base):
    """value = fr（正分数）按 mode 舍入到 p 位尾数。
    返回 (mant, exp)；范围检查由调用方处理。"""
    # 结果指数 e_r = floor_log_base(sqrt(fr)) − (p−1)
    lo = floor_log(fr, base)  # floor_log_base(fr)
    lo_sqrt = lo // 2  # floor_log_base(sqrt(fr)) = floor(lo/2)
    e_r = lo_sqrt - (p - 1)
    # S = fr × base^(−2·e_r)，sqrt(S) ∈ [base^(p−1), base^p)
    S = fr * Fraction(base) ** (-2 * e_r)
    # 整数化：S = num / den（den = base^j）
    num, den = S.numerator, S.denominator
    # den 是 base 的幂：den = base^j
    j = 0
    d = den
    while d > 1:
        d //= base
        j += 1
    # 计算 floor(sqrt(S)) = floor(isqrt(num × base^(2k−j)) / base^k)
    # 选 k = ceil(j/2) 使 2k ≥ j
    k = (j + 1) // 2
    scaled = num * (base ** (2 * k - j))
    q = isqrt(scaled)  # floor(sqrt(scaled))
    q_floor = q // (base ** k)  # floor(sqrt(S))
    # S 是否恰为平方：q² × base^j == num
    exact_sq = (q_floor * q_floor) * den == num
    if mode in (R_NEAREST,):
        # 最近偶：比较 sqrt(S) 与 q_floor + 0.5
        # sqrt(S) vs q+0.5 ⟺ 4S vs (2q+1)² ⟺ 4·num vs (2q+1)²·den
        lhs = 4 * num
        rhs = (2 * q_floor + 1) ** 2 * den
        if lhs > rhs:
            r = q_floor + 1
        elif lhs < rhs:
            r = q_floor
        else:
            r = q_floor if (q_floor % 2 == 0) else q_floor + 1  # tie→even
        return r, e_r
    if mode in (R_TZ, R_TN):
        return q_floor, e_r          # 正值向零/向−∞ = floor
    if mode in (R_TP, R_AWAY):
        return q_floor + (0 if exact_sq else 1), e_r  # ceil
    raise AssertionError("mode")


def map_special(v):
    f = v[0]
    if f == F_NAN:
        return (F_NAN, 0, 0)
    if f == F_NEG_INF:
        return (F_NAN, 0, 0)  # sqrt(−∞) = NaN（IEEE / §11）
    return (f, 0, 0)  # +inf → +inf


def sqrt_ref_bf(v, ctx):
    """C 的 bf_sqrt 语义参考：特殊值/零 + 精确边界舍入 + 范围检查。"""
    p, emin, emax, mode = ctx.p, ctx.emin, ctx.emax, ctx.mode
    if is_special(v):
        return map_special(v)
    if is_zero(v):
        return v
    if v[0] == F_NEG:
        return (F_NAN, 0, 0)
    fr = value_fraction(v, 2)
    neg = v[0] == F_NEG
    mant, e = sqrt_boundary_exact(fr, p, mode, 2)
    # 范围检查（与 round_ctx 相同：上溢按模式 ±inf/最大有限；下溢 flush）
    if e > emax:
        to_inf = (mode in (R_NEAREST, R_AWAY)) \
            or (mode == R_TP and not neg) or (mode == R_TN and neg)
        if to_inf:
            return (F_POS_INF, 0, 0)
        return (F_POS, (1 << p) - 1, emax)
    if e < emin:
        return (F_POS_ZERO, 0, 0)   # sqrt 结果恒非负
    return (F_POS, mant, e)


def sqrt_ref_bd(v, ctx):
    p, emin, emax, mode = ctx.p, ctx.emin, ctx.emax, ctx.mode
    if is_special(v):
        return map_special(v)
    if is_zero(v):
        return v
    if v[0] == F_NEG:
        return (F_NAN, 0, 0)
    fr = value_fraction(v, 10)
    mant, e = sqrt_boundary_exact(fr, p, mode, 10)
    if e > emax:
        to_inf = (mode in (R_NEAREST, R_AWAY)) \
            or (mode == R_TP) or (mode == R_TN and False)
        if to_inf:
            return (F_POS_INF, 0, 0)
        return (F_POS, 10 ** p - 1, emax)
    if e < emin:
        return (F_POS_ZERO, 0, 0)
    # 规范化（去尾随零）
    while mant % 10 == 0:
        mant //= 10
        e += 1
    return (F_POS, mant, e)


def fmt(v):
    return "%d %d %d" % v


def fmt_pair(v):
    return "%d %d %d %d %d %d" % (v[0][0], v[0][1], v[0][2],
                                  v[1][0], v[1][1], v[1][2])


def run_driver(driver, lines):
    proc = subprocess.run([driver], input="\n".join(lines),
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print("driver exit %d: %s" % (proc.returncode, proc.stderr))
        sys.exit(1)
    return proc.stdout.splitlines()


def main():
    bf_drv = sys.argv[1] if len(sys.argv) > 1 \
        else os.environ.get("NEX_BF_DRIVER", "bigfloat_golden_driver")
    bd_drv = sys.argv[2] if len(sys.argv) > 2 \
        else os.environ.get("NEX_BD_DRIVER", "bigdecimal_golden_driver")

    rng = random.Random(RAND_SEED)
    bf_lines = []
    bd_lines = []
    bf_expect = []
    bd_expect = []
    bf_ctx = None

    def set_bf(ctx):
        nonlocal bf_ctx
        bf_ctx = ctx
        bf_lines.append("bf_ctx %d %d %d" % (ctx.p, ctx.exp_bits, ctx.mode))
        bf_expect.append("R ok")

    def set_bd(ctx):
        bd_lines.append("bd_ctx %d %d %d" % (ctx.p, ctx.exp_bits, ctx.mode))
        bd_expect.append("R ok")

    for _ in range(COUNT):
        p, eb, mode = (rng.choice([24, 53, 113]), rng.choice([8, 11, 15]),
                       rng.randrange(5))
        ctx = gbf_ctx(p, eb, mode)
        nd = rng.randrange(1, 30)
        digits = str(rng.randrange(1, 10)) + "".join(
            str(rng.randrange(10)) for _ in range(nd - 1))
        e10 = rng.randrange(-eb, eb)
        s = "%s.%se%d" % (digits[:1], digits[1:], e10)
        v = gbf_parse(s, ctx)
        set_bf(ctx)
        bf_lines.append("bf_sqrt %s" % s)
        bf_expect.append("R " + fmt(sqrt_ref_bf(v, ctx)))

        pd, ebd, moded = (rng.choice([7, 16, 34]), rng.choice([2, 3, 4]),
                          rng.randrange(5))
        dctx = gbd_ctx(pd, ebd, moded)
        nd2 = rng.randrange(1, 30)
        digits2 = str(rng.randrange(1, 10)) + "".join(
            str(rng.randrange(10)) for _ in range(nd2 - 1))
        e10d = rng.randrange(-ebd, ebd)
        ds = "%s.%se%d" % (digits2[:1], digits2[1:], e10d)
        dv = gbd_parse(ds, dctx)
        set_bd(dctx)
        bd_lines.append("bd_sqrt %s" % ds)
        bd_expect.append("R " + fmt(sqrt_ref_bd(dv, dctx)))

    # 特殊值用例（固定 ctx）
    set_bf(gbf_ctx(53, 11, 0))
    set_bd(gbd_ctx(16, 3, 0))
    for s in ["0", "-0", "1", "4", "2", "0.25", "inf", "nan", "-inf",
              "-5", "1e-100", "9e-1", "5e-324", "1e300"]:
        v = gbf_parse(s, gbf_ctx(53, 11, 0))
        bf_lines.append("bf_sqrt %s" % s)
        bf_expect.append("R " + fmt(sqrt_ref_bf(v, gbf_ctx(53, 11, 0))))
        dv = gbd_parse(s, gbd_ctx(16, 3, 0))
        bd_lines.append("bd_sqrt %s" % s)
        bd_expect.append("R " + fmt(sqrt_ref_bd(dv, gbd_ctx(16, 3, 0))))

    bf_outs = run_driver(bf_drv, bf_lines)
    bd_outs = run_driver(bd_drv, bd_lines)
    fails = 0
    for line, out, want in zip(bf_lines, bf_outs, bf_expect):
        if out != want:
            print("MISMATCH bf: %s: got %r want %r" % (line, out, want))
            fails += 1
    for line, out, want in zip(bd_lines, bd_outs, bd_expect):
        if out != want:
            print("MISMATCH bd: %s: got %r want %r" % (line, out, want))
            fails += 1
    if fails:
        print("GOLDEN FAIL: %d mismatches" % fails)
        sys.exit(1)
    print("GOLDEN OK (bf %d + bd %d sqrt cases, 独立边界比较法)"
          % (len(bf_expect), len(bd_expect)))


def gbf_ctx(p, eb, mode):
    import golden_bigfloat as gbf
    return gbf.Ctx(p, eb, mode)


def gbf_parse(s, ctx):
    import golden_bigfloat as gbf
    return gbf.parse_decimal(s, ctx)


def gbd_ctx(p, eb, mode):
    import golden_bigdecimal as gbd
    return gbd.Ctx(p, eb, mode)


def gbd_parse(s, ctx):
    import golden_bigdecimal as gbd
    return gbd.parse_decimal(s, ctx)


if __name__ == "__main__":
    main()
