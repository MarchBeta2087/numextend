/*
 * nex_alloc.c：库内统一分配器实现。
 *
 * nex_test_alloc_fail_after 语义：
 *   - SIZE_MAX（默认）：不注入，走标准 malloc / realloc；
 *   - N（> 0）：第 N 次分配失败（计数器递减，N 次后每次返回 NULL）；
 *   - 0：立即失败（所有分配返回 NULL）。
 * 计数器只减不增，测试须在每轮注入后重置为 SIZE_MAX。
 */

#include "nex/nex_alloc.h"

#include <stdlib.h>

size_t nex_test_alloc_fail_after = SIZE_MAX;

void *nex_malloc(size_t size)
{
    if (nex_test_alloc_fail_after != SIZE_MAX) {
        if (nex_test_alloc_fail_after == 0U) {
            return NULL;  // 立即失败
        }
        nex_test_alloc_fail_after--;
        if (nex_test_alloc_fail_after == 0U) {
            return NULL;  // 第 N 次分配失败
        }
    }
    return malloc(size);
}

void *nex_realloc(void *ptr, size_t size)
{
    if (nex_test_alloc_fail_after != SIZE_MAX) {
        if (nex_test_alloc_fail_after == 0U) {
            return NULL;
        }
        nex_test_alloc_fail_after--;
        if (nex_test_alloc_fail_after == 0U) {
            return NULL;
        }
    }
    return realloc(ptr, size);
}
