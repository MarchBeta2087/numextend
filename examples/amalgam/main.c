/*
 * examples/amalgam/main.c：单文件组合（amalgamation）示例。
 *
 * 场景：**十进制财务求和** —— 用 bigdecimal 精确相加若干金额，验证十进制
 * 语义无二进制浮点舍入误差（例如 0.1 + 0.2）。
 *
 * 这是「无需 CMake / 无需安装、单文件分发」的用法：只依赖生成的两文件
 * `nex_amalgam.c` / `nex_amalgam.h`。构建：
 *
 *   python3 scripts/amalgamate.py --out dist
 *   cc -O2 -std=c99 -Idist examples/amalgam/main.c dist/nex_amalgam.c -o amalgam_demo
 *   ./amalgam_demo
 */

#include "nex_amalgam.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const bigdecimal_ctx_ty ctx = bigdecimal_ctx_decimal64();  // 16 位有效数字
    const char *prices[] = {"19.99", "0.01", "1234567.89", "0.10", "0.20"};
    const size_t count = sizeof(prices) / sizeof(prices[0]);

    bigdecimal_ty total;
    if (bigdecimal_init(&total) != BIGDECIMAL_OK_E) {
        return 1;
    }
    (void)bigdecimal_from_str(&total, "0", &ctx, NULL);

    for (size_t i = 0U; i < count; i++) {
        bigdecimal_ty item;
        bigdecimal_init(&item);
        (void)bigdecimal_from_str(&item, prices[i], &ctx, NULL);
        (void)bigdecimal_add(&total, &total, &item, &ctx);
        bigdecimal_free(&item);
    }

    char buf[128];
    size_t need = 0U;
    if (bigdecimal_to_str(&total, BIGDECIMAL_FMT_FIXED_E,
                          buf, sizeof(buf), &need) != BIGDECIMAL_OK_E) {
        bigdecimal_free(&total);
        return 1;
    }
    printf("nex %s (amalgam): decimal total = %s\n", NEX_VERSION_STRING, buf);

    const int ok = (strcmp(buf, "1234588.19") == 0);
    bigdecimal_free(&total);
    return ok ? 0 : 1;
}
