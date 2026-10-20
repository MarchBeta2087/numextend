# CBMC 有界验证（P2-C）

对 `nex/bigint/bin` 的**信任基**做有界模型检查（bounded model checking），
作为 P2「Lean 形式化」的替代（决策见 `TODO.md` §K4、issue #98）：

- **证明**（不是抽样测试）输入肢数 ≤3、肢值任意且规范化时，加/减/比较
  与一份独立的 16-bit digit 参考模型逐 digit 一致；
- 乘法检查无 UB、结果规范化/符号正确，以及常数乘子 0/1/2/3 的功能正确性；
- 同时打开越界、指针、有符号溢出、移位、循环展开断言。

## 运行

```sh
sudo apt-get install -y cbmc     # 或 brew install cbmc
sh scripts/verify-cbmc.sh        # 无 cbmc 时 SKIP；CI 用 NEX_CBMC_REQUIRE=1
```

CI job：`linux-cbmc`。本机约 30 s（`JOBS=6`），40 个目标。

## 方法

- **肢数固定为编译期常量**（`-DIN_LIMBS=<a> -DIN_LIMBS_B=<b>`，0..3）。
  符号化长度会让 CBMC 的堆模型/循环展开公式爆炸；固定后 0..3 与
  等长/不等长组合逐一验证，即为有界完备。
- **不等长是必需的**：`mag_add` / `mag_sub` 的尾随进位/借位循环只在
  `len(a) != len(b)` 时执行（例如 `harness_add 1 3`）。只跑等长会漏掉
  这段代码——本 harness 的变异测试（把尾随循环的 `>> 32` 改成 `>> 33`）
  正是靠 `1x3` 目标抓到的。
- **参考模型独立实现**：16-bit digit、符号-幅值，不复用被测代码。
- **别名**：`dst == lhs` 走 `add_sub` 的临时对象路径（`harness_*_alias`）。
- `--unwinding-assertions` 保证展开边界足够（`unwind = 4 + 4*max(a,b)`），
  否则验证失败。

## 未覆盖 / 已知边界

- **乘法的完整功能等价**（任意 a×b 逐 digit）对 SAT 是硬问题：单个
  16-bit digit 断言约 1 分钟，1 肢全量约 5 分钟。`harness_mul` 保留在源码
  中可手动运行，但不进 CI；更大的乘法正确性由 D（差分/属性模糊测试）覆盖。
- **OOM 路径**（`nex_test_alloc_fail_after` 注入）未建模，默认 malloc 恒成功。
- **不检查无符号回绕**：库内（和参考模型）有意使用 `i-- > 0` 这类写法，
  C 语义良定义；有符号溢出仍检查。若需要，可用 `__CPROVER_assert` 对具体
  不变量（如进位 ≤ 1）单独断言。
- 不覆盖除法、NTT/FFT、dec 模块、浮点/ball —— 属于 D 的范围。

## 变异测试（防「空验证」）

已验证两类注入缺陷会被抓到：

| 变异 | 目标 | 结果 |
|---|---|---|
| `mag_add` 首循环 `carry = sum >> 33` | `harness_add 3x3` | FAIL（`digit matches`） |
| `mag_add` 尾循环 `carry = sum >> 33` | `harness_add 1x3` | FAIL（`normalized top limb`） |

改动 harness 后请至少重跑一次这类变异，确认断言仍然敏感。
