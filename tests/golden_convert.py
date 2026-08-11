#!/usr/bin/env python3
"""nex_convert 黄金对拍：随机生成测试用例，C 驱动 vs Python 精确参考。

参考复用 golden_bigfloat / golden_bigdecimal 的 round_ctx（Fraction 精确
复刻标量语义）：有损转换（→ bigfloat / bigdecimal）以**精确有理数中间值**
单次舍入（§10 原则），与 C 的保护位整数除法独立对拍；精确转换（→ bigfrac
/ bigint）直接以 Fraction 精确值比较。

用法：
    python golden_convert.py [driver_path]

driver_path 默认为 ./golden_convert_driver；可通过环境变量
NEX_GOLDEN_DRIVER 覆盖。全部通过打印 GOLDEN OK 并退出 0。
"""
import os
import random
import subprocess
import sys
from fractions import Fraction

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

# CI/控制台编码容错：非 ASCII 输出以占位符代替，避免 cp1252 等
# 编码环境下 UnicodeEncodeError 崩溃
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")

import golden_bigfloat as gbf
import golden_bigdecimal as gbd

DRIVER = os.environ.get(
    "NEX_GOLDEN_DRIVER",
    sys.argv[1] if len(sys.argv) > 1 else "./golden_convert_driver")

RAND_SEED = int(os.environ.get("NEX_GOLDEN_SEED", "20260812"))

F_POS_ZERO, F_NEG_ZERO, F_POS, F_NEG, F_POS_INF, F_NEG_INF, F_NAN = range(7)


def is_special(v):
    return v[0] in (F_POS_INF, F_NEG_INF, F_NAN)


def is_zero(v):
    return v[0] in (F_POS_ZERO, F_NEG_ZERO)


def fmt_bf(v):
    return "%d %d %d" % v


def fmt_bd(v):
    return "%d %d %d" % v


def value_fraction(v):
    """浮点值 (flag, mant, exp) → 精确 Fraction；特殊值返回 None。"""
    if is_special(v):
        return None
    f, mant, exp = v
    fr = Fraction(mant) * Fraction(2) ** exp
    return -fr if f == F_NEG else fr


def value_fraction_dec(v):
    if is_special(v):
        return None
    f, mant, exp = v
    fr = Fraction(mant) * Fraction(10) ** exp
    return -fr if f == F_NEG else fr


def map_special(v, target):
    """特殊值按目标语义映射。"""
    f = v[0]
    if f == F_NAN:
        return (F_NAN, 0, 0)
    neg = f == F_NEG_INF
    if target == "bd":
        return (F_NEG_INF if neg else F_POS_INF, 0, 0)
    return (F_NEG_INF if neg else F_POS_INF, 0, 0)


def rand_float_str(rng, eb):
    nd = rng.randrange(1, 30)
    digits = str(rng.randrange(1, 10)) + "".join(
        str(rng.randrange(10)) for _ in range(nd - 1))
    e10 = rng.randrange(-(eb - 2), eb)
    return "%s.%se%d" % (digits[:1], digits[1:], e10)


def rand_dec_str(rng, eb):
    nd = rng.randrange(1, 30)
    digits = str(rng.randrange(1, 10)) + "".join(
        str(rng.randrange(10)) for _ in range(nd - 1))
    e10 = rng.randrange(-(eb - 1), eb)
    return "%s.%se%d" % (digits[:1], digits[1:], e10)


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
    fctx = None  # 当前 bigfloat ctx
    dctx = None

    def emit(line, want):
        lines.append(line)
        expected.append(want)

    def set_fctx(ctx):
        nonlocal fctx
        fctx = ctx
        emit("cv_ctx_bf %d %d %d" % (ctx.p, ctx.exp_bits, ctx.mode), "R ok")

    def set_dctx(ctx):
        nonlocal dctx
        dctx = ctx
        emit("cv_ctx_bd %d %d %d" % (ctx.p, ctx.exp_bits, ctx.mode), "R ok")

    # 随机：float ↔ decimal、frac → float/decimal、dec → float、精确互转
    for _ in range(300):
        fctx_c = gbf.Ctx(rng.choice([24, 53, 113]), rng.choice([8, 11, 15]),
                         rng.randrange(5))
        dctx_c = gbd.Ctx(rng.choice([7, 16, 34]), rng.choice([2, 3, 4]),
                         rng.randrange(5))
        set_fctx(fctx_c)
        set_dctx(dctx_c)

        fs = rand_float_str(rng, fctx_c.exp_bits)
        ds = rand_dec_str(rng, dctx_c.exp_bits)
        fv = gbf.parse_decimal(fs, fctx_c)       # bigfloat 值（浮点语义）
        dv = gbd.parse_decimal(ds, dctx_c)       # bigdecimal 值

        # float → decimal（±0 保号：转换保留零符号）
        if is_special(fv):
            want = "R " + fmt_bd(map_special(fv, "bd"))
        elif is_zero(fv):
            want = "R " + fmt_bd((fv[0], 0, 0))
        else:
            want = "R " + fmt_bd(gbd.round_ctx(value_fraction(fv), dctx_c))
        emit("cv_float_to_decimal %s" % fs, want)
        # decimal → float
        if is_special(dv):
            want = "R " + fmt_bf(map_special(dv, "bf"))
        elif is_zero(dv):
            want = "R " + fmt_bf((dv[0], 0, 0))
        else:
            want = "R " + fmt_bf(gbf.round_ctx(value_fraction_dec(dv), fctx_c))
        emit("cv_decimal_to_float %s" % ds, want)

        # frac → float / decimal（随机分数）
        n = rng.randrange(-10 ** 20, 10 ** 20)
        d = rng.randrange(1, 10 ** 20)
        ns = str(n)
        ds2 = str(d)
        emit("cv_frac_to_float %s %s" % (ns, ds2),
             "R " + fmt_bf(gbf.round_ctx(Fraction(n, d), fctx_c)))
        emit("cv_frac_to_decimal %s %s" % (ns, ds2),
             "R " + fmt_bd(gbd.round_ctx(Fraction(n, d), dctx_c)))

        # dec → float（大整数）
        big = rng.randrange(0, 10 ** 40)
        emit("cv_dec_to_float %d" % big,
             "R " + fmt_bf(gbf.round_ctx(Fraction(big), fctx_c)))

        # float → frac / decimal → frac（精确）
        fr = value_fraction(fv)
        if fr is None:
            emit("cv_float_to_frac %s" % fs, "E invalid")
        else:
            emit("cv_float_to_frac %s" % fs,
                 "R %d %d" % (fr.numerator, fr.denominator))
        frd = value_fraction_dec(dv)
        if frd is None:
            emit("cv_decimal_to_frac %s" % ds, "E invalid")
        else:
            emit("cv_decimal_to_frac %s" % ds,
                 "R %d %d" % (frd.numerator, frd.denominator))

        # float → bin / decimal → bin（仅整数）
        if fr is not None and fr.denominator == 1:
            emit("cv_float_to_bin %s" % fs, "R %d" % fr.numerator)
        else:
            emit("cv_float_to_bin %s" % fs, "E invalid")
        if frd is not None and frd.denominator == 1:
            emit("cv_decimal_to_bin %s" % ds, "R %d" % frd.numerator)
        else:
            emit("cv_decimal_to_bin %s" % ds, "E invalid")

    # 特殊值与固定用例
    fctx_c = gbf.Ctx(53, 11, 0)
    dctx_c = gbd.Ctx(16, 3, 0)
    set_fctx(fctx_c)
    set_dctx(dctx_c)
    for fs in ["0", "-0", "0.5", "-3.5", "1000", "inf", "-inf", "nan",
               "0.1", "1e300", "5e-324"]:
        fv = gbf.parse_decimal(fs, fctx_c)
        fr = value_fraction(fv)
        emit("cv_float_to_frac %s" % fs,
             "R %d %d" % (fr.numerator, fr.denominator) if fr is not None
             else "E invalid")
        emit("cv_float_to_bin %s" % fs,
             "R %d" % fr.numerator if (fr is not None and fr.denominator == 1)
             else "E invalid")
        if is_special(fv):
            emit("cv_float_to_decimal %s" % fs, "R " + fmt_bd(map_special(fv, "bd")))
        elif is_zero(fv):
            emit("cv_float_to_decimal %s" % fs, "R " + fmt_bd((fv[0], 0, 0)))
        else:
            emit("cv_float_to_decimal %s" % fs,
                 "R " + fmt_bd(gbd.round_ctx(fr, dctx_c)))
    for ds in ["0", "1.5", "-12.34", "0.25", "inf", "nan", "100", "1e-10"]:
        dv = gbd.parse_decimal(ds, dctx_c)
        frd = value_fraction_dec(dv)
        emit("cv_decimal_to_frac %s" % ds,
             "R %d %d" % (frd.numerator, frd.denominator)
             if frd is not None else "E invalid")
        emit("cv_decimal_to_bin %s" % ds,
             "R %d" % frd.numerator if (frd is not None and frd.denominator == 1)
             else "E invalid")
        if is_special(dv):
            emit("cv_decimal_to_float %s" % ds, "R " + fmt_bf(map_special(dv, "bf")))
        elif is_zero(dv):
            emit("cv_decimal_to_float %s" % ds, "R " + fmt_bf((dv[0], 0, 0)))
        else:
            emit("cv_decimal_to_float %s" % ds,
                 "R " + fmt_bf(gbf.round_ctx(frd, fctx_c)))
    # 复数 → 实数
    for zs in ["1+0i", "0+0i", "1+2i", "3.5-0i", "nan+0i"]:
        emit("cv_cpx_float_to_float %s" % zs, "R cpx")   # 占位
        emit("cv_cpx_decimal_to_decimal %s" % zs, "R cpx")
    # 大整数边界
    for big in ["1", "-1", "0", "99999999999999999999", "-12345678901234567890"]:
        emit("cv_dec_to_float %s" % big,
             "R " + fmt_bf(gbf.round_ctx(Fraction(int(big)), fctx_c)))

    outs = run_driver(lines)
    if len(outs) != len(expected):
        print("line count mismatch: %d vs %d" % (len(outs), len(expected)))
        sys.exit(1)
    fails = 0
    for i, (line, out, want) in enumerate(zip(lines, outs, expected)):
        if line.startswith("cv_cpx_float_to_float"):
            zs = line.split(" ", 1)[1]
            re_s = zs[:-3] if zs.endswith("+0i") or zs.endswith("-0i") \
                else zs[:-2] if zs.endswith("0i") else zs
            re_s = re_s.lstrip("-")
            exp = "E invalid" if zs.endswith("2i") else \
                "R " + fmt_bf(gbf.parse_decimal(re_s, fctx))
            if out != exp:
                print("MISMATCH: %s: got %r want %r" % (line, out, exp))
                fails += 1
            continue
        if line.startswith("cv_cpx_decimal_to_decimal"):
            zs = line.split(" ", 1)[1]
            re_s = zs[:-3] if zs.endswith("+0i") or zs.endswith("-0i") \
                else zs[:-2] if zs.endswith("0i") else zs
            re_s = re_s.lstrip("-")
            exp = "E invalid" if zs.endswith("2i") else \
                "R " + fmt_bd(gbd.parse_decimal(re_s, dctx))
            if out != exp:
                print("MISMATCH: %s: got %r want %r" % (line, out, exp))
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
