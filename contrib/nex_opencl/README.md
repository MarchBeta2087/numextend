# contrib/nex_opencl：OpenCL 批量大整数（issue #98 P3）

默认不构建；`-DNEX_ENABLE_OPENCL=ON` 时作为独立实验构建，**不进**核心
`libnex`、`nex_modules.txt`、子集闭包与 amalgam，也不改核心 API/ABI。

## 状态

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 骨架、动态加载、设备枚举、CI（PoCL CPU 正确性） | ✅ |
| M1 | `madd`（逐元素 `c=a*b+c`）与 `dot`（一批点积） | ✅ |
| M2 | 矩阵×向量流式（4 GB 显存 safety zone、VRAM > RAM > 磁盘） | ⏳ 计划 |

## M1 实测（Windows，RTX 4050 Laptop 6 GB，Release）

工作负载：`n = 10000`，每个数 104 个 32-bit limb = 3328 bit ≈ 1000 位十进制。

| 路径 | 耗时 | 说明 |
|---|---:|---|
| CPU 单线程 `madd`（同算法） | 741 ms | 16-bit digit schoolbook |
| CPU 单线程 `dot`（同算法） | 763 ms | 乘积 + 列和 |
| **GPU `madd` 端到端** | **6.9 ms** | 含 3 输入上传 + 结果回读，min/5 |
| **GPU `dot` 端到端** | **4.6 ms** | 含 2 输入上传 + 结果回读，min/5 |
| CPU 16 线程 `parallel_vector` | 14.85 ms | `a*b+c`，bigfloat/Karatsuba |

- `madd` ≈ **2.1×** 于现有 16 线程批量路径，≈ 106× 于单线程同算法；
- 数据量：a/b/c 各 4.16 MB，结果 8.36 MB（209 limb），host 有效流量约 3.0 GB/s；
- 正确性：GPU 与 CPU 参考逐位一致，CPU 参考与 `bigint_bin` 交叉验证一致
  （`nex_opencl_test`，覆盖 limbs = 1/2/8/104、n = 1/3/17/257、全 1 与随机）。

## 数据表示与 API

- 每个数为固定 `limbs` 个 **32-bit limb（小端）**，不足高位补零；
  1000 位十进制用 `limbs = 104`。
- kernel 内把 32-bit limb 拆成 16-bit digit 做乘加：单元素列和最大
  `208 × 65535² < 2^40`，用 64-bit 累加；点积/矩阵的跨项和用 64-bit/32-bit
  累加（见下），最后一次进位。
- 只支持非负幅值；符号由调用方处理。

```c
nex_opencl_ctx_ty *ctx = nex_opencl_create(104, err, sizeof(err));
nex_opencl_madd(ctx, a, b, c, out, n);   /* out[i] = a[i]*b[i] + c[i] */
nex_opencl_dot(ctx, a, b, out, n);       /* out = Σ a[i]*b[i] */
nex_opencl_destroy(ctx);
```

结果 limb 数固定 `2*limbs + 1`（乘积 2*limbs，+1 进位余量）。
`dot` 的 `n` 上限 2³²（和的额外高位由最后 1 limb 覆盖）。
上下文不共享，库内无全局状态；每个 ctx 自带 queue/buffer。

## 实现要点

- **动态加载**：Windows `OpenCL.dll` / Linux `libOpenCL.so.1`，不依赖 SDK
  或 import lib；`nex_opencl_abi.h` 是最小 OpenCL 1.2 ABI 子集。
- **设备选择**：默认优先 GPU，其次 CPU；`NEX_OPENCL_DEVICE=<子串>` 可指定
  设备（如 `NVIDIA`、`Iris`）。
- **两阶段乘法**：`colsum` 求每列无进位 64-bit 和，`carry` 每元素串行进位；
  `dot` 用 `dotreduce` 在设备端按 digit 归约，再一次进位。
- **buffer 复用**：按容量缓存输入/列和/输出缓冲；三个输入写为非阻塞，
  一次 queue 内顺序执行。

## 构建与测试

```sh
cmake -S . -B build -DNEX_ENABLE_OPENCL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/contrib/nex_opencl/nex_opencl_test
./build/contrib/nex_opencl/nex_opencl_bench 10000 104
```

无 OpenCL 平台/设备时 `nex_opencl_test` 打印 SKIP 并返回 0；设
`NEX_OPENCL_REQUIRE=1` 可强制失败。CI `linux-opencl` 用
`ocl-icd-opencl-dev + pocl-opencl-icd` 在 CPU device 上验证正确性（不做
性能断言）；GPU 性能数在本机（RTX 4050）记录。

## M2：矩阵×向量（10k×10k，4 GB 显存）

**约束已量化**：10⁴×10⁴ 稠密、每元素 416 B ⇒ 矩阵 **41.6 GB**，大于本机
RAM（31.7 GB）和显存（4 GB safety zone），必须流式。

计划：
1. 行分块（tile），`x` 常驻显存（4.16 MB），A 按 tile 从 RAM/NVMe 读入；
   双缓冲 overlap 磁盘/PCIe 与 kernel；
2. 每行/每 tile 复用 `colsum` 的乘积列和，按 digit 累加到 `uint32`
   （10⁴ 项 × 65535 < 2³⁰，无溢出），全部列累加完后一次进位；
3. 优先 VRAM，其次 RAM，最后磁盘；4 GB 显存内按 `tile_rows × 10k × 416 B`
   反推 tile 行数（如 64 行 ≈ 266 MB/缓冲）；
4. 验收：10k×10k 端到端 ≥3× 于 16 线程 CPU（按 148 s 基线），峰值显存
   <4 GB；若矩阵在 RAM/显存则走快路径。

## 限制

- 仅非负幅值；不支持负数/符号位。
- `limbs` 在 ctx 创建时固定（kernel 特化）；不同位宽需多个 ctx。
- `dot` 的 `n` ≤ 2³²；更大规模需增加高位余量。
- Windows 端当前为阻塞回读 + 非阻塞写；pinned/异步传输留 M2。
- 实验代码，不承诺 API/ABI；默认 OFF、不进核心。
