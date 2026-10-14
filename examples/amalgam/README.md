# 单文件组合示例（amalgamation）

演示**无需 CMake、无需安装**、只用一个 C 编译器即可消费 Numextend 的方式：
把整个库（或子集）组合成 `nex_amalgam.c` + `nex_amalgam.h` 两个文件。

本示例是「十进制财务求和」场景：用 `bigdecimal` 精确相加金额，验证十进制
语义没有二进制浮点舍入误差。

## 构建与运行

```sh
# 1) 生成单文件组合（默认全部模块；也可 --modules bigdecimal,bigint_dec 裁剪）
python3 scripts/amalgamate.py --out dist

# 2) 编译（C99，无外部依赖）
cc -O2 -std=c99 -Idist examples/amalgam/main.c dist/nex_amalgam.c -o amalgam_demo

# 3) 运行
./amalgam_demo
```

预期输出：

```
nex <版本> (amalgam): decimal total = 1234588.19
```

也可把 `dist/nex_amalgam.c` 与 `dist/nex_amalgam.h` 直接拷入自己的工程。
