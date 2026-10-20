# contrib：实验目录索引

本目录收录**默认不构建**的隔离实验。所有实验都**不进入**核心 `libnex`、
`nex_modules.txt`、子集闭包与 amalgam，也**不为它们开核心规范特例**
（design §14）。各自用显式 CMake 选项启用。

## 索引

| 目录 | 内容 | 跟踪 | 开关 | 状态 |
|---|---|---|---|---|
| `nex_thread` | 跨平台最小线程池（pthread / Win32 `CONDITION_VARIABLE`） | TODO §F | `-DNEX_ENABLE_THREAD=ON` | 可用，CI `linux-thread-tsan`（TSan） |
| `nex_parallel` | 线程池的两个消费方：并行 NTT 乘法（A）与批量 `a*b+c`（B） | TODO §F | 同上 | 可用，收益已量化 |
| `nex_bigint_block` | 分块 NTT 乘法：三分块（P1a）→ 递归多级分块（P1b），突破直接 NTT 上限 | issue #98 | `-DNEX_ENABLE_BIGNUM=ON` | 1e9 位 2403 s / 6.24 GB；2e9 位递归验证；1e10 位 OOM |
| `nex_ssa` | Schönhage–Strassen 原型（`2^L+1` / `2^(L/2)+1` 双环 + CRT） | issue #102 | `-DNEX_ENABLE_SSA=ON` | M1 完成：1e6–1e8 位，1e8 位 1142–1318 s / 1.66 GB；**M2/M3 已决策不做**（2026-10-20） |
| `nex_ooc` | out-of-core 分块乘法：肢体文件 + 按块落盘，峰值内存 O(块长) | issue #104 | `-DNEX_ENABLE_BIGNUM=ON` | M1 完成：1e9 位 164 MB 峰值逐位校验；1e8 大×大 227 s / 652 MB 与内存 NTT 一致；**M2/M3 已决策不做**，实用上限 1e9 位级 |
| `nex_opencl` | OpenCL 批量大整数：逐元素 `a*b+c` + 点积（M1），矩阵×矩阵子集（M2） | issue #98 P3 | `-DNEX_ENABLE_OPENCL=ON` | M1 完成：10k×1000 位 madd ≈6.9 ms / dot ≈4.6 ms；M2 子矩阵乘 4k×4k 60+60 并集 327 s / 6.1×10⁶ big-MADD/s；M3 稀疏 d=0.1 4.8 s（RTX 4050），逐位一致；CI `linux-opencl` 用 PoCL 做正确性 |

各目录另有自己的 `README.md`（算法、参数模型、实测表）。规划状态见
`TODO.md` §K，里程碑与验收标准以 Issue 为准。

CI 覆盖：`nex_thread` / `nex_parallel` 由 `linux-thread-tsan`（TSan）构建并跑
`parallel_ntt` 自检；`nex_bigint_block` / `nex_ssa` / `nex_ooc` 由
`linux-bignum` 构建并跑各目录自检；`nex_opencl` 由 `linux-opencl`（PoCL CPU）
构建并跑正确性测试。

---

# 线程池与并行 NTT 实验（`nex_thread` / `nex_parallel`）

本部分是 **TODO §F 的隔离实验**：跨平台线程池 + 两个消费方，用于回答
“线程是否值得进入产品”。它**不进入**核心 `libnex`、`nex_modules.txt`、
子集闭包或 amalgam；默认不构建。

## 构建 / 运行

```bash
cmake -S . -B build-thread -DNEX_ENABLE_THREAD=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-thread -j
# 单次大乘法（A）
./build-thread/contrib/nex_parallel/nex_parallel_test
./build-thread/contrib/nex_parallel/nex_parallel_bench
# 批量独立运算（B）
./build-thread/parallel_vector 1024 30000
```

- `NEX_ENABLE_THREAD=OFF`（默认）时核心构建完全不变。
- 后端：pthread（Linux/macOS）或 Win32 `CONDITION_VARIABLE`（MSVC/MinGW）。
- 线程池由调用方创建/销毁；库内无全局状态、无锁。

## A. 单次大规模乘法（并行 NTT）

`nex_parallel_bin_mul_ntt` 复用公开 `nex_ntt` API，强制双模数 CRT NTT：

- 变换长度 `n < 65536`：4 路任务（两个模数 × a/b 的正变换）+ 2 路点乘/逆变换；
- `n ≥ 65536`：**扁平任务图**——每个 stage 只做一次 barrier，同时处理
  a0/b0/a1/b1 四个缓冲区；点乘/逆变换同理；
- Garner 系数重构按 k 并行；进位仍串行（O(n)，64k 肢下约 <1% 总时间），
  结果与核心 NTT 路径**逐位一致**。

本机 16 硬件线程，Release（单位 ms，speedup 相对核心 `bigint_bin_mul`）：

| 肢数 | 核心 | 1 | 2 | 4 | 8 | 16 |
|---:|---:|---:|---:|---:|---:|---:|
| 16384 | 20.5 | 0.96× | 1.58× | 2.66× | 2.33× | **2.50×** |
| 32768 | 45.1 | 0.93× | 1.57× | 2.26× | 2.64× | **3.11×** |
| 65536 | 96.8 | 0.95× | 1.55× | 2.20× | 3.05× | **3.58×** |
| 131072 | 280.6 | 0.89× | 1.83× | 2.94× | 4.01× | **4.92×** |

（3 轮 min-of-3。）结论：扁平任务图 + 并行 Garner + 分块进位后，单次
大乘法从约 2.5–2.8× 提升到 **2.5–4.9×**；4M bit（131072 肢）在 16 线程
达到 **4.9×**（8 线程 4.0×）。仍非线性（内存带宽、块间进位前缀、任务图
开销）；1 线程有约 4–11% 的额外开销。

### 盈亏平衡点（相对核心 AUTO）

注意：16384 肢以下核心走 **Toom-3**，不是串行 NTT；因此“并行 NTT vs 核心”
的盈亏平衡比文献中“2^15 bit 可接近线性”的经验阈值更靠后。表中 1024–8192
肢走 4 路核心 NTT，16384 肢起走扁平任务图；16 线程为 8 物理核 ×2 逻辑线程。

| 肢数（bit） | 核心算法 | 核心 ms | 16 线程并行 ms | 加速 |
|---:|---|---:|---:|---:|
| 1024（32k） | Toom-3 | 0.355 | 0.545 | **0.65×** |
| 2048（64k） | Toom-3 | 1.02 | 1.02 | **1.00×** |
| 4096（128k） | Toom-3 | 2.51 | 2.09 | **1.20×** |
| 8192（256k） | Toom-3 | 7.61 | 4.21 | **1.81×** |
| 16384（512k） | NTT | 20.5 | 8.2 | **2.50×** |
| 32768（1M） | NTT | 45.1 | 14.5 | **3.11×** |
| 65536（2M） | NTT | 96.8 | 27.0 | **3.58×** |
| 131072（4M） | NTT | 280.6 | 57.0 | **4.92×** |

- 本实现的盈亏平衡约在 **2048–4096 肢（64k–128k bit）**；有明显收益要到
  **≥8192 肢（256k bit）**，4M bit 达到 4.9×。更小的规模并行开销/打包/CRT
  反而使性能下降。
- 与文献并不矛盾：Java `parallelMultiply` 约 2.75×、8 核并行 Karatsuba
  5.12–6.14×、8/32/64 线程 SSA 6.41–21.42×，对应的是更大的操作数
  （千万级 64-bit 字，约 6.4 亿 bit，是本文 4M bit 的 ~150 倍）与更成熟的
  并行任务图。本实验只有 2 个模数，且 4M bit 仍处于内存带宽与并行度受限
  区间。
- 继续推到 5–6× 的候选：并行 pack/memcpy、把块间进位前缀也做成并行扫描、
  在 8M–64M bit 规模复测，并减少每 stage 的唤醒开销。

## B. 大批量独立运算（高维向量式）

`examples/parallel_vector`：`out[i] = a[i]·b[i] + c[i]`，固定 256 元素/任务；
结果与串行逐元素精确比较。

| 精度 × 数量 | 串行 ms | 1 | 2 | 4 | 8 | 16 |
|---|---:|---:|---:|---:|---:|---:|
| 256 bit × 100k | 62.7 | 1.01× | 1.78× | 3.36× | 4.92× | **6.55×** |
| 1024 bit × 30k | 43.8 | 1.01× | 1.95× | 3.68× | 5.57× | **7.02×** |
| 4096 bit × 8k | 96.1 | 0.99× | 2.03× | 3.82× | 5.43× | **7.73×** |

结论：**批量独立运算才是线程池的主场**，16 线程接近线性（6.5–7.7×），且不
需要改核心、不改变结果。

## 决策（与 design §14 / TODO §F 一致）

1. **不为 `contrib/` 开核心规范特例**：它保持独立可选目标，不进 `libnex`、
   `amalgam` 与子集闭包；核心“零依赖 / C99 / 单文件 / 不内部加锁”契约不变。
2. 单次乘法并行收益有限（≤4.9×，且仅 ≥256k bit 明显）且需要复制 NTT
   内核，**不作为核心特性**。
3. 批量并行收益明确，但通用线程池（OpenMP/TBB）已覆盖该需求；本 contrib
   的差异是 C99 + 确定性 + 双后端。要升级为一等能力，应独立仓库/包
   （如 `libnex-thread`），并补稳定性承诺（TSan CI 已接入）。
4. 当前仍是实验：未承诺 API/ABI；`NEX_ENABLE_THREAD=ON` 显式启用，
   TSan CI 见 `linux-thread-tsan`。
