#ifndef NEX_DEBUG_INTERNAL_H
#define NEX_DEBUG_INTERNAL_H

/*
 * nex_debug_internal.h：内部不变量断言（P2-D，issue #98）。
 *
 * 默认关闭（release 零开销）；构建时加 -DNEX_DEBUG_INVARIANTS=1
 * （CMake 选项 NEX_DEBUG_INVARIANTS）启用。断言只检查**便宜且恒定成立**
 * 的内部不变式（进位/借位上界、规范化），不做 O(n²) 复核；语义级性质
 * （ball 包含性、浮点指数范围等）由 fuzz harness 检查。
 */

#if defined(NEX_DEBUG_INVARIANTS)
#include <assert.h>
#define NEX_INVARIANT(cond) assert(cond)
#else
#define NEX_INVARIANT(cond) ((void)0)
#endif

#endif /* NEX_DEBUG_INTERNAL_H */
