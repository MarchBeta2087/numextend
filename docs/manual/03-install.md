# 3. 安装与使用

## 3.1 构建与安装

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build --parallel
cmake --install build
```

安装内容：公开头文件、静态库、共享库（可选）、CMake 包配置与 pkg-config 文件。

- 默认构建**静态库**；追加 `-DBUILD_SHARED_LIBS=ON` 构建**共享库**
  （Linux/macOS 带 `SOVERSION`，如 `libnex.so.1`）。
- 要求：C99 编译器（GCC / Clang / MSVC）与 CMake ≥ 3.16；默认标准 C99，
  可用 `-DNEX_C_STANDARD=11/17/23` 切换验证。C++（11–26）可直接链接（§3.6）。

## 3.2 在 CMake 工程中使用

```cmake
find_package(nex CONFIG REQUIRED)
target_link_libraries(app PRIVATE nex::nex)
```

配合 `-DCMAKE_PREFIX_PATH=/your/prefix` 指定安装位置。完整示例见仓库
`examples/downstream/`。C++ 工程同样使用 `find_package(nex CONFIG REQUIRED)`
与 `nex::nex`；公开头文件已带 `extern "C"`，无需额外包装。

## 3.3 使用 pkg-config

```sh
cc app.c $(pkg-config --cflags --libs nex) -o app
```

## 3.4 最小示例

```c
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_version.h"
#include <stdio.h>

int main(void) {
    bigint_bin_ty a, b, r;
    bigint_bin_init(&a);
    bigint_bin_init(&b);
    bigint_bin_init(&r);
    bigint_bin_from_str(&a, "123456789012345678901234567890", 10, NULL);
    bigint_bin_from_str(&b, "987654321098765432109876543210", 10, NULL);
    bigint_bin_mul(&r, &a, &b);

    size_t need = 0;
    bigint_bin_to_str(&r, 10, NULL, 0, &need);
    char *buf = (char *)malloc(need + 1);
    bigint_bin_to_str(&r, 10, buf, need, &need);
    printf("nex %s: %s\n", NEX_VERSION_STRING, buf);

    free(buf);
    bigint_bin_free(&r);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
    return 0;
}
```

## 3.5 直接内嵌源码

库无外部依赖，也可直接 `add_subdirectory(numextend)` 并链接目标 `nex`，
无需安装。

## 3.6 在 C++ 中使用

公开头文件均带 `extern "C"` 守卫，C++ 直接包含即可：

```cpp
#include "nex/bigint/bin/nex_bigint_bin.h"

int main() {
    bigint_bin_ty a{};
    bigint_bin_init(&a);
    /* ... */
    bigint_bin_free(&a);
    return 0;
}
```

无需 `extern "C"` 包装或单独的语言绑定；库本身可继续以 C 编译，C++ 消费者
经 C ABI 链接（已由 `tests/test_cpp_consumer.cpp` 回归）。
