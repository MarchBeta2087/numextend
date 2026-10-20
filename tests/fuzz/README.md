# 属性/差分模糊测试（P2-D）

单个 libFuzzer 入口 `fuzz_nex.c`，按输入首字节选择 6 类风险面。目标不是
"跑覆盖率"，而是**用独立参考/代数恒等式判定结果对错**：

| 模式 | 检查 |
|---:|---|
| 0 | NTT：正逆变换往返；循环卷积 vs schoolbook（随机内置模数、长度 4–64） |
| 1 | CRT：已知值重构精确；任意余数重构后逐模数回代一致 |
| 2 | dec↔bin：十进制/十六进制字符串往返；dec→bin 与 bin 直接解析逐位一致 |
| 3 | bigfrac：分母为正、既约（gcd==1）、零唯一表示；`(x+y)-y == x`；`x·(1/x) == 1` |
| 4 | bigfloat：正常值尾数位数 == `mant_bits`、指数在 `[emin, emax]`、`compose∘decompose` 幂等 |
| 5 | ball：`add`/`mul` 的区间结果必须包含同精度（最近舍入）参考值 |

同时使用 ASan + UBSan，并打开 `-DNEX_DEBUG_INVARIANTS=ON`（进位/借位上界、
规范化断言），因此越界、UB、内部不变式破坏都会直接变成 fuzz 崩溃。

## 运行

```sh
cmake -S . -B build-fuzz -DCMAKE_C_COMPILER=clang \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEX_ENABLE_FUZZ=ON \
      -DNEX_DEBUG_INVARIANTS=ON
cmake --build build-fuzz --target fuzz_nex --parallel
mkdir -p build-fuzz/corpus
cp -r tests/fuzz/corpus/. build-fuzz/corpus/     # 种子只读，生成物留在 build 下
./build-fuzz/tests/fuzz_nex build-fuzz/corpus -max_total_time=120
```

CI job `linux-fuzz`：固定 120 s 预算（种子来自 `tests/fuzz/corpus/`）。
本机（clang 18）约 3.9k exec/s、峰值 RSS ~0.8 GB。

## 种子

`corpus/` 每个模式一个最小种子（首字节 = 模式号），**不要**把 libFuzzer
生成的单元提交进来（运行目录用 `build-fuzz/corpus`）。新增模式或发现新的
边界输入时，把最小化后的样本放进 `tests/fuzz/corpus/`，CI 会自动继承。

## 变异测试（防「空 fuzz」）

已验证：给 `ntt_inverse` 的归一化乘子注入 `+1 mod p` 后，模式 0 在
200 次运行内报 `FAIL mode=0: ntt roundtrip`。改动 harness 后请重跑一次
这类注入，确认断言仍然敏感。

## 边界

- 规模刻意很小（NTT ≤64 点、大数 ≤4 肢），保证 fuzz 吞吐；大规模正确性
  由黄金对拍（Python/mpmath）与 CBMC（`verify/cbmc`）分别覆盖。
- 不建模 OOM 注入（`nex_test_alloc_fail_after` 已有独立用例 `test_oom`）。
- 模式 5 的"参考值"是同精度最近舍入结果；它必须落在定向舍入得到的区间内。
  若 ball 实现把区间做得过窄，会立刻触发。
- 单次 120 s 只是回归门槛，不是覆盖率证明；加时用 `-max_total_time`。
