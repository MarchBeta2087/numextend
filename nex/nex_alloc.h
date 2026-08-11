#ifndef NEX_ALLOC_H
#define NEX_ALLOC_H

#include <stddef.h>
#include <stdint.h>

/*
 * nex_alloc：库内统一分配器抽象（内部使用）。
 *
 * 目的：
 *   1. 归一化分配入口，供 OOM 注入测试（设计 §12"错误路径注入失败分配
 *      验证清理路径"）：nex_test_alloc_fail_after 设为 N 时，第 N 次及
 *      之后的分配返回 NULL（0 = 立即失败；SIZE_MAX = 不失败，默认）；
 *   2. 为后续自定义分配器钩子（§13 后续方向）预留单一入口。
 *
 * 语义：
 *   - nex_malloc / nex_realloc 失败返回 NULL（与标准库一致）；realloc
 *     失败时原指针仍有效（调用方须保证该模式下的状态一致性，即 OOM 时
 *     输出参数不变——库内 ensure_cap 等均遵循）；
 *   - 计数器仅在库内部可见，测试经公开函数 nex_test_alloc_fail_after
 *     直接设置（头文件内声明，测试包含本头文件即可）。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 分配失败注入计数器（SIZE_MAX = 不注入，默认）。测试直接赋值。 */
extern size_t nex_test_alloc_fail_after;

void *nex_malloc(size_t size);
void *nex_realloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NEX_ALLOC_H */
