#!/usr/bin/env python3
"""大整数规模/内存/工作量可行性计算器（P0，issue #98）。

用法：
    python3 scripts/bigint-feasibility.py [digits ...]
    python3 scripts/bigint-feasibility.py --json

默认列出 1e6 / 1e7 / 1e8 / 1e9 / 1e10 / 1e12 十进制位。

说明：
- 内存为等长两操作数 + 结果的粗略上界；NTT 数组按核心 mul_ntt 的
  a/b/c1 三个变换长度 n 估算，SSA 按 6 个 n 的数组估算（仅量级）。
- 当前核心 NTT 硬上限：sum(limbs) ≤ 2^27，变换长度 log2n ≤ 28
  （内置模数 c 最大 30），等长约 6.5 亿十进制位。
- 时间只给相对工作量 n·log2(n)，绝对时间需由 bench_huge 实测标定。
"""

import argparse
import json
import math
import sys

LOG2_10 = math.log2(10.0)
NTT_MAX_SUM = 1 << 27
NTT_MAX_LOG2N = 28
CURRENT_MAX_LIMBS = NTT_MAX_SUM // 2
CURRENT_MAX_DIGITS = CURRENT_MAX_LIMBS * 32.0 / LOG2_10


def pow2_ceil(x):
    n = 1
    while n < x:
        n <<= 1
    return n


def analyse(digits):
    bits = digits * LOG2_10
    limbs = int(math.ceil(bits / 32.0))
    packed_bytes = limbs * 4
    # 等长操作数：s = 2·limbs；卷积长度 2s−1
    s = 2 * limbs
    need = max(2 * s - 1, 2 * limbs)
    n = pow2_ceil(need)
    log2n = n.bit_length() - 1
    ntt_ok = (s <= NTT_MAX_SUM) and (log2n <= NTT_MAX_LOG2N)
    # 核心 mul_ntt：a,b,c1 各 n 个 uint32；操作数/结果约 4×packed
    ntt_bytes = 3 * n * 4 + 4 * packed_bytes
    # 分块 NTT：按 max_len 折半，三个半积；每个半积 n_block
    half = (limbs + 1) // 2
    s_block = 2 * half
    need_block = max(2 * s_block - 1, 2 * half)
    n_block = pow2_ceil(need_block)
    log2n_block = n_block.bit_length() - 1
    block_ok = (s_block <= NTT_MAX_SUM) and (log2n_block <= NTT_MAX_LOG2N)
    # 三个半积结果 + 一次半积的 NTT 数组 + 操作数/结果
    block_bytes = 3 * (2 * packed_bytes) + 3 * n_block * 4 + 4 * packed_bytes
    # SSA 粗估：6 个 n 数组 + 操作数/结果
    ssa_bytes = 6 * n * 4 + 4 * packed_bytes
    work = n * max(log2n, 1)
    return {
        "digits": digits,
        "bits": bits,
        "limbs": limbs,
        "packed_mb": packed_bytes / (1024.0 * 1024.0),
        "transform_n": n,
        "log2n": log2n,
        "ntt_ok": ntt_ok,
        "block_ok": block_ok,
        "ntt_mem_mb": ntt_bytes / (1024.0 * 1024.0),
        "block_mem_mb": block_bytes / (1024.0 * 1024.0),
        "ssa_mem_mb": ssa_bytes / (1024.0 * 1024.0),
        "work": work,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("digits", nargs="*", type=float,
                    help="十进制位数列表（默认 1e6..1e12）")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    values = args.digits or [1e6, 1e7, 1e8, 1e9, 1e10, 1e12]
    rows = [analyse(v) for v in values]
    if args.json:
        print(json.dumps({
            "log2_10": LOG2_10,
            "ntt_max_sum": NTT_MAX_SUM,
            "ntt_max_log2n": NTT_MAX_LOG2N,
            "current_max_digits": CURRENT_MAX_DIGITS,
            "rows": rows,
        }, indent=2, ensure_ascii=False))
        return 0

    print("大整数规模/内存/工作量可行性（P0）\n")
    print("当前直接 NTT 上限：sum(limbs) ≤ 2^27，log2n ≤ 28"
          "（内置模数 c ≤ 30；c=28/30 各一个）；等长上限约 %.1f 亿十进制位。"
          "更高位宽可走 contrib 分块（三个半积）。\n"
          % (CURRENT_MAX_DIGITS / 1e8))
    print("| 十进制位 | bit | 32-bit 肢 | 单操作数 | 变换 n | log2n | 直接 | "
          "分块 | NTT 内存 | 分块内存 | SSA 内存 | 相对工作量 |")
    print("|---:|---:|---:|---:|---:|---:|:---:|:---:|---:|---:|---:|---:|")
    base = rows[0]["work"] if rows else 1
    for r in rows:
        print("| %.3g | %.3g | %.3g | %.1f MB | %.3g | %d | %s | %s | "
              "%.1f MB | %.1f MB | %.1f MB | %.3g× |"
              % (r["digits"], r["bits"], r["limbs"], r["packed_mb"],
                 r["transform_n"], r["log2n"],
                 "yes" if r["ntt_ok"] else "**no**",
                 "yes" if r["block_ok"] else "**no**",
                 r["ntt_mem_mb"], r["block_mem_mb"], r["ssa_mem_mb"],
                 r["work"] / base))
    print("\n注：内存为粗略上界，实际峰值以 bench_huge 的 Peak RSS 为准；"
          "时间仅相对工作量，绝对时间需实测标定。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
