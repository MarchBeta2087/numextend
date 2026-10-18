# contrib：线程池与并行 NTT 实验

本目录是 **TODO §F 的隔离实验**：跨平台线程池 + 两个消费方，用于回答
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

- 变换长度 `n < 262144`：4 路任务（两个模数 × a/b 的正变换）+ 2 路点乘/逆变换；
- `n ≥ 262144`：每次 forward/inverse 内部按 stage 蝶形单元并行（固定分块）；
- CRT/进位串行；结果与核心 NTT 路径**逐位一致**。

本机 16 硬件线程，Release（单位 ms，speedup 相对核心 `bigint_bin_mul`）：

| 肢数 | 核心 | 1 | 2 | 4 | 8 | 16 |
|---:|---:|---:|---:|---:|---:|---:|
| 16384 | 22.0 | 0.93× | 2.00× | **2.52×** | 2.48× | 2.53× |
| 32768 | 45.1 | 0.93× | 1.55× | **2.45×** | 2.24× | 2.37× |
| 65536 | 107.7 | 1.00× | 1.55× | 2.04× | 2.58× | **2.80×** |

结论：单次大乘法可获得约 **2.5–2.8×**，但远非线性（内存带宽、4 路任务上限、
stage 屏障）。1 线程有约 0–7% 的打包/CRT 额外开销。

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
2. 单次乘法并行收益有限（≤2.8×）且需要复制 NTT 内核，**不作为核心特性**。
3. 批量并行收益明确，但通用线程池（OpenMP/TBB）已覆盖该需求；本 contrib
   的差异是 C99 + 确定性 + 双后端。要升级为一等能力，应独立仓库/包
   （如 `libnex-thread`），并补 TSan CI 与稳定性承诺。
4. 当前仍是实验：未接 CI、未承诺 API/ABI；`NEX_ENABLE_THREAD=ON` 显式启用。
