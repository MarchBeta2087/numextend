/*
 * subset_smoke.c：子集构建冒烟测试（TODO §B2）。
 *
 * 仅在 NEX_MODULES != all 且依赖闭包含 bigint_bin 时构建。只使用 bigint_bin
 * 公开 API；其闭包自带 bin/dec/conv/fft/ntt/alloc，用以验证裁剪后的库能正常
 * 链接与运算（含进制 10 字符串 I/O，会触达 bin ↔ conv 的依赖环）。
 */

#include <stdio.h>
#include <string.h>

#include "nex/bigint/bin/nex_bigint_bin.h"

int main(void) {
    bigint_bin_ty a, b, s;
    char buf[64];
    size_t need = 0;

    if (bigint_bin_init(&a) != BIGINT_OK_E) {
        return 1;
    }
    if (bigint_bin_init(&b) != BIGINT_OK_E) {
        return 1;
    }
    if (bigint_bin_init(&s) != BIGINT_OK_E) {
        return 1;
    }
    (void)bigint_bin_from_u64(&a, 12345u);
    (void)bigint_bin_from_u64(&b, 6789u);
    if (bigint_bin_add(&s, &a, &b) != BIGINT_OK_E) {
        return 1;
    }
    if (bigint_bin_to_str(&s, 10, buf, sizeof(buf), &need) != BIGINT_OK_E) {
        return 1;
    }
    if (strcmp(buf, "19134") != 0) {
        fprintf(stderr, "subset_smoke FAIL: got '%s'\n", buf);
        return 1;
    }

    bigint_bin_free(&s);
    bigint_bin_free(&b);
    bigint_bin_free(&a);
    printf("subset_smoke OK: 12345 + 6789 = %s\n", buf);
    return 0;
}
