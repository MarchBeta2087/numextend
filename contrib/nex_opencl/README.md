# contrib/nex_opencl：OpenCL 批量大整数（issue #98 P3）

默认不构建；`-DNEX_ENABLE_OPENCL=ON` 时作为独立实验构建，**不进**核心
`libnex`、`nex_modules.txt`、子集闭包与 amalgam，也不改核心 API/ABI。

## 状态

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 骨架、动态加载、设备枚举、CI（PoCL CPU 正确性） | ✅ |
| M1 | `madd`（逐元素 `c=a*b+c`）与 `dot`（一批点积） | ✅ |
| M2 | 矩阵×矩阵子集（4k×4k，60 行 + 60 列并集，稠密） | ✅ |
| M3 | 稀疏 CSR/CSC + 密度扫描（`--density d`） | ✅ |

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

## M2：矩阵×矩阵子集（4k×4k，稠密）

目标（按实际需求调整后）：`C = A×B`，A/B 都是 **4096×4096**、每元素 1000 位
（104 limb），只取 **随机 60 行 + 随机 60 列** 的并集：

- 输出数：60×4096 + 4096×60 − 60×60 = **487,920**；
- 大数乘法：487,920 × 4096 ≈ **2.0×10⁹**；
- 数据：A/B 各 **6.98 GB**（进 RAM，不进 4 GB 显存），输出 **408 MB**；
- 显存：按 tile 装入 A 行块 + B 列块，默认预算 2.5 GB（4 GB safety zone）。

### 实测（RTX 4050，Windows，Release）

```sh
./build/contrib/nex_opencl/nex_opencl_bench matmul 4096 4096 4096 60 60
```

| 阶段 | 耗时 |
|---|---:|
| GPU：60 行 × 4096 列 | 162.6 s |
| GPU：4096 行 × 60 列 | 164.5 s |
| **GPU 总计（含 host 打包/传输）** | **327.1 s（5.45 min）** |
| 吞吐 | **6.11×10⁶ big-MADD/s** |

- 16 个抽样输出与 `bigint_bin` **逐位一致**；
- CPU 投影：串行 ~6.0 h，16 线程 ~**22.5 min**（<1 h；按规则可运行，本 bench
  未完整执行）。GPU 约为 16 线程 CPU 的 **4.1×**；
- 规模校准（同一内核）：

| m=k=n | 选取 | GPU 总耗时 | 吞吐 |
|---:|---:|---:|---:|
| 512 | 16 行/16 列 | 1.46 s | 5.66×10⁶/s |
| 1024 | 32/32 | 10.95 s | 6.03×10⁶/s |
| 2048 | 30/30 | 41.47 s | 6.02×10⁶/s |
| 4096 | 60/60 | 327.1 s | 6.11×10⁶/s |

### 实现

- kernel `matmul_sub`：**一个 workgroup 负责一个输出**（64 线程），
  A/B 元素按 p 载入 local memory，64 个线程分摊 4L+1 个 16-bit digit 的
  列和；`ulong` 累加（k×2L×65535² < 2⁶⁴），最后一次进位。
- host 侧 `nex_opencl_matmul_subset(ctx, A, lda, B, ldb, rows, nrows, cols,
  ncols, C, ldc, k)`：
  - 选行/选列，按 `NEX_OPENCL_MATMUL_TILE`（可选）与 2.5 GB 预算选 tile；
  - tile 外循环只上传一次，内循环流式上传另一侧；
  - 输出按 `ldc` scatter 回调用方数组。
- 稠密 A/B 走 RAM；显存只放 tile。NVMe 流式留给更大的矩阵（本次不需要）。

### 后续（稀疏）

“多种稠密度”尚未利用：当前 kernel 对零元素仍做完整乘加。下一步按
CSR/CSC 存非零项，dot 只遍历行/列非零索引的交集；在 4k×4k 上做
density = 1.0 / 0.5 / 0.1 / 0.01 的扫描，找 GPU/CPU 交叉点。

## M3：稀疏密度扫描（4k×4k，60+60 并集）

输入为 CSR（A 按行）与 CSC（B 按列），索引升序；kernel 只归并两边非零
索引的交集，零元素不参与乘加。

```sh
nex_opencl_bench matmul 4096 4096 4096 60 60 --density 0.1
```

| density | A/B nnz（约） | GPU 总耗时 | CPU 16 线程投影 | GPU/CPU |
|---:|---:|---:|---:|---:|
| 1.0（稠密路径） | 16.8M / 16.8M | 327.1 s | 22.5 min | **4.1×** |
| 0.5 | 8.39M / 8.39M | 88.7 s | 360 s | **4.1×** |
| 0.1 | 1.68M / 1.68M | 4.77 s | 10.4 s | **2.2×** |
| 0.05 | 0.84M / 0.84M | 1.78 s | 3.3 s | 1.9× |
| 0.01 | 168k / 168k | 0.52 s | 0.6 s | ~1.1× |
| 0.005 | 84k / 84k | 0.36 s | 0.4 s | ~1.1× |
| 0.001 | 17k / 17k | 0.41 s | <0.1 s | CPU 更快 |

- 每次运行 16 个抽样输出与 `bigint_bin` **逐位一致**；
- 低密度下 GPU 有约 **0.4 s 固定开销**（host 打包 + 256 个 tile 上传/launch），
  交叉点在 **density ≈ 0.005–0.01**：再低就应由 CPU 处理；
- 稀疏数据量：d=0.1 时 A+B 的 values 约 **1.4 GB**（vs 稠密 14 GB）；
- 索引/值只在 tile 内上传，显存仍按 2.5 GB 预算。

### 调用

```c
nex_opencl_csr_ty A = { m, nnz, row_ptr, col_idx, values };
nex_opencl_csc_ty B = { n, nnz, col_ptr, row_idx, values };
nex_opencl_matmul_sparse(ctx, &A, &B, rows, nrows, cols, ncols, C, ldc);
```

`row_ptr` / `col_idx` 与 `values` 的布局与稠密接口一致（values 为
`nnz × limbs`）；索引必须升序。

## 限制

- 仅非负幅值；不支持负数/符号位。
- `limbs` 在 ctx 创建时固定（kernel 特化）；不同位宽需多个 ctx。
- `dot` 的 `n` ≤ 2³²；更大规模需增加高位余量。
- Windows 端当前为阻塞回读 + 非阻塞写；pinned/异步传输留后续优化。
- 实验代码，不承诺 API/ABI；默认 OFF、不进核心。
