# 2. 通用约定

本章约定适用于全部模块。

## 2.1 错误码

每个模块有自己的错误枚举（`bigint_err_ty`、`bigfloat_err_ty`、
`bigdecimal_err_ty`、`bigfrac_err_ty`、`bigcomplex_*_err_ty` 等），均以
`*_OK_E = 0` 表示成功，其余为错误。常见错误：

| 错误 | 含义 |
|------|------|
| `*_ERR_OOM_E` | 内存分配失败 |
| `*_ERR_INVALID_E` | 非法参数（空指针、非法进制等） |
| `*_ERR_DIV_ZERO_E` | 除数为零 |
| `*_ERR_OVERFLOW_E` | 结果超出目标类型范围（如转 `uint64_t`） |
| `*_ERR_PARSE_E` | 字符串解析失败 |
| `*_ERR_UNSUPPORTED_E` | 已登记但当前版本未实现 |

## 2.2 内存与 OOM（强异常安全）

任何因 OOM 或非法参数失败的运算，**输出参数保持调用前的状态**。因此可以在
已有对象上安全地重试或回退。示例：`bigint_bin_mul(&c, &a, &b)` 若返回
`BIGINT_ERR_OOM_E`，则 `c` 与调用前完全一致。

## 2.3 别名（aliasing）

算术函数的输出允许与任一输入**别名**，例如 `bigint_bin_mul(&a, &a, &b)`
（就地累积）与 `bigint_bin_mul(&a, &b, &a)` 均合法。每个函数注释会注明是否
允许别名（本库算术函数普遍允许）。

## 2.4 线程安全

库内**不加锁**：同一对象不可被多线程并发访问；不同对象之间可并行使用。
调用方负责外部同步。

## 2.5 版本与 ABI

- 库版本经 `nex/nex_version.h` 暴露（`NEX_VERSION_MAJOR/MINOR/PATCH/STRING`），
  与构建系统的 `project(VERSION)` 单一来源。
- **API 稳定性**：1.x 只做向后兼容的增量。
- **ABI 稳定性**：同一主版本内共享库可直接替换（`SOVERSION = 主版本号`）；
  公开结构体的字段顺序/大小/对齐在同一主版本内冻结，但字段视为**实现细节**，
  可移植代码应使用访问器而非依赖布局。

## 2.6 端序

实现基于 `uint32_t`/`uint64_t` 的值语义，预期与端序无关；当前仅在**小端**
平台（x86-64 / arm64）验证，大端未测试。
