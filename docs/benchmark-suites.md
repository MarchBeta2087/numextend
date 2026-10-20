# 基准 suite 清单（自动生成）

> 由 `python3 scripts/bench.py --list --markdown` 生成，勿手改；新增/修改 suite 请编辑 `bench/suites.json`。

| suite | modules | contrib | tier | 预估 s | timeout s | RSS MB |
|---|---|---|---|---:|---:|---:|
| `core.smoke` | bigint_bin, ntt, bigfloat | — | ci | 10 | 60 | 512 |
| `core.sweep.ci` | bigint_bin, ntt, fft | — | ci | 70 | 120 | 1024 |
| `core.math.ci` | bigfloat, bigdecimal, bigcomplex_float, transcend | — | ci | 130 | 180 | 1024 |
| `core.sweep.deep` | bigint_bin, ntt, fft | — | quick | 480 | 1000 | 2048 |
| `core.math.quick` | bigfloat, bigdecimal, transcend, series | — | quick | 240 | 300 | 2048 |
| `contrib.block.1e6` | bigint_bin, ntt | nex_bigint_block | quick | 30 | 180 | 1024 |
| `contrib.parallel.ntt` | ntt, bigint_bin | nex_parallel | quick | 120 | 600 | 2048 |
| `contrib.opencl.madddot` | bigint_bin | nex_opencl | quick | 10 | 120 | 1024 |
| `contrib.opencl.matmul.512` | bigint_bin | nex_opencl | quick | 30 | 300 | 1024 |
| `contrib.opencl.cpu.512` | bigint_bin | nex_opencl | quick | 30 | 300 | 1024 |
| `core.sweep.full` | bigint_bin, ntt, fft | — | full | 3600 | 3900 | 4096 |
| `core.math.full` | bigfloat, bigdecimal, transcend, series, ball | — | full | 5400 | 6000 | 4096 |
| `core.huge.1e8` | bigint_bin, ntt | — | full | 300 | 900 | 2048 |
| `contrib.block.1e8` | bigint_bin, ntt | nex_bigint_block | full | 600 | 1800 | 8192 |
| `contrib.opencl.matmul.4k` | bigint_bin | nex_opencl | full | 400 | 3600 | 20480 |
| `contrib.ssa.1e6` | bigint_bin, ntt | nex_ssa | frozen | 60 | 600 | 2048 |
| `contrib.ooc.1e7` | bigint_bin, ntt | nex_ooc | frozen | 60 | 600 | 1024 |
| `contrib.ooc.1e8` | bigint_bin, ntt | nex_ooc | frozen | 300 | 1200 | 2048 |
