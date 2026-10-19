#!/usr/bin/env python3
"""SSA（Schönhage–Strassen）参数/内存模型（P1b-SSA M1，issue #102）。

标准参数：N bit 整数 → n = 2^(k−1) 个 m = 2^(k−1) bit 的块，
环 Z/(2^(2m)+1)，变换长度 L = 2n = 2^k；环元素 2m = 2^k bit。

仅用于量级评估；实际实现可用更少数组/分块/out-of-core。
"""

import argparse
import json
import math
import sys

LOG2_10 = math.log2(10.0)


def analyse(digits, arrays=3):
    nbits = digits * LOG2_10
    # n = m = 2^(k-1)，且 n·m ≥ N
    k = max(1, math.ceil((math.log2(nbits) + 2.0) / 2.0))
    m = 1 << (k - 1)
    n = 1 << (k - 1)
    L = 1 << k
    ring_bits = 2 * m
    ring_bytes = ring_bits / 8.0
    fft_bytes = L * ring_bytes
    packed_bytes = nbits / 8.0
    total = arrays * fft_bytes + 4 * packed_bytes
    return {
        "digits": digits,
        "bits": nbits,
        "k": k,
        "n_pieces": n,
        "piece_bits": m,
        "transform_len": L,
        "ring_bits": ring_bits,
        "ring_bytes": ring_bytes,
        "fft_array_mb": fft_bytes / (1024.0 * 1024.0),
        "arrays": arrays,
        "packed_mb": packed_bytes / (1024.0 * 1024.0),
        "total_mb": total / (1024.0 * 1024.0),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("digits", nargs="*", type=float,
                    default=None, help="默认 1e8/1e9/1e10/1e12")
    ap.add_argument("--arrays", type=int, default=3,
                    help="同时驻留的 FFT 数组数（默认 3）")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    vals = args.digits or [1e8, 1e9, 1e10, 1e12]
    rows = [analyse(v, args.arrays) for v in vals]
    if args.json:
        print(json.dumps({"rows": rows}, indent=2, ensure_ascii=False))
        return 0
    print("SSA 参数/内存模型（M1；数组数=%d）\n" % args.arrays)
    print("| 十进制位 | bit | k | 块数 n | 块 bit m | 变换 L | 环 bit | "
          "单数组 | 总内存 |")
    print("|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        print("| %.3g | %.3g | %d | %.3g | %.3g | %.3g | %.3g | %.1f MB | "
              "%.1f MB（%.1f GB） |"
              % (r["digits"], r["bits"], r["k"], r["n_pieces"],
                 r["piece_bits"], r["transform_len"], r["ring_bits"],
                 r["fft_array_mb"], r["total_mb"], r["total_mb"] / 1024.0))
    print("\n注：标准 SSA 的 FFT 数组为 L 个环元素（每个 ring_bits）；"
          "实际实现可通过递归/分块/out-of-core 降低峰值。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
