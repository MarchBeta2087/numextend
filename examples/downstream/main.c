/*
 * downstream/main.c：独立下游示例（仅经 find_package(nex) 消费已安装的库）。
 *
 * 计算 30! 并打印，同时展示库版本宏。
 */
#include "nex/bigint/bin/nex_bigint_bin.h"
#include "nex/nex_version.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    bigint_bin_ty fact;
    bigint_bin_init(&fact);
    bigint_bin_from_u64(&fact, 1U);

    for (unsigned i = 1U; i <= 30U; i++) {
        bigint_bin_ty t;
        bigint_bin_init(&t);
        bigint_bin_from_u64(&t, i);
        bigint_bin_mul(&fact, &fact, &t);
        bigint_bin_free(&t);
    }

    size_t need = 0U;
    bigint_bin_to_str(&fact, 10U, NULL, 0U, &need);
    char *buf = (char *)malloc(need + 1U);
    bigint_bin_to_str(&fact, 10U, buf, need, &need);
    printf("nex v%s: 30! = %s\n", NEX_VERSION_STRING, buf);

    free(buf);
    bigint_bin_free(&fact);
    return 0;
}
