#ifndef NEX_BIGINT_BIN64_H
#define NEX_BIGINT_BIN64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/nex_bigint_common.h"

/*
 * nex_bigint_bin64：64 位肢二进制大整数（设计文档 §13 #3"64 位肢变体"）。
 *
 * 与 bigint_bin 平行：基 2^64、小端序、同一规范化不变式（最高有效肢非零；
 * 零 ⇔ len == 0 且 sign == ZERO_E）与错误码枚举（bigint_err_ty）。
 *
 * 64 位肢的意义：同一位长下肢数减半，加法 / 减法 / schoolbook 乘法的
 * 循环次数减半（实测 ~2 倍提速，见 bench）。代价是乘法的 64×64→128
 * 中间量——C99 无 128 位扩展，经编译器探测条件编译（§13 #3）：
 *   - GCC / Clang：__SIZEOF_INT128__ 可用时走硬件 128 位乘法；
 *   - MSVC 及其他：回退便携 32 位半字 4 乘（nex_u128_mul 的 portable 路径）。
 *
 * 本模块为独立变体，不参与 bigint_bin / bigint_dec 的互转与字符串 I/O
 * （v1 仅提供 加减乘 与基本转换，其余按需扩展）。
 */

/* 64 位肢大整数 */
typedef struct {
    bigint_sign_ty sign;  // 三标志：正 / 负 / 零（与 bin / dec 共用枚举）
    uint64_t *limbs;      // 肢数组，小端序，基 2^64
    size_t len;           // 当前有效肢数
    size_t cap;           // 已分配容量（肢数）
} bigint_bin64_ty;

/* 128 位无符号整数（64×64→128 乘法的产物） */
typedef struct {
    uint64_t lo;  // 低 64 位
    uint64_t hi;  // 高 64 位
} nex_u128_ty;

/*
 * brief: 64×64→128 乘法（内部基础设施，暴露供测试直接校验）
 * note: 内部函数，不属公开 API；实现自动选择硬件 __int128 或便携 4 乘
 */
nex_u128_ty nex_u128_mul(uint64_t a, uint64_t b);

/* ------------------------------------------------------------------ */
/* 生命周期                                                             */
/* ------------------------------------------------------------------ */

/*
 * brief: 初始化为零（limbs 可为 NULL，cap 0）
 */
bigint_err_ty bigint_bin64_init(bigint_bin64_ty *val);

/*
 * brief: 初始化并预分配 cap 肢容量
 */
bigint_err_ty bigint_bin64_init_cap(bigint_bin64_ty *val, size_t cap);

/*
 * brief: 释放，容忍零初始化对象（free 后 limbs 置 NULL）
 */
void bigint_bin64_free(bigint_bin64_ty *val);

/*
 * brief: 深拷贝（dst 与 src 允许不同对象）
 */
bigint_err_ty bigint_bin64_copy(bigint_bin64_ty *dst,
        const bigint_bin64_ty *src);

/*
 * brief: 接管资源（src 置零）
 */
void bigint_bin64_move(bigint_bin64_ty *dst, bigint_bin64_ty *src);

/*
 * brief: 收缩容量至 len
 */
bigint_err_ty bigint_bin64_shrink(bigint_bin64_ty *val);

/* ------------------------------------------------------------------ */
/* 基本转换与断言                                                       */
/* ------------------------------------------------------------------ */

bigint_err_ty bigint_bin64_from_u64(bigint_bin64_ty *val, uint64_t value);
bigint_err_ty bigint_bin64_to_u64(const bigint_bin64_ty *val, uint64_t *out);
int bigint_bin64_cmp(const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);
bool bigint_bin64_is_zero(const bigint_bin64_ty *val);

/* ------------------------------------------------------------------ */
/* 加减乘                                                               */
/* ------------------------------------------------------------------ */

bigint_err_ty bigint_bin64_add(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);
bigint_err_ty bigint_bin64_sub(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);
bigint_err_ty bigint_bin64_mul(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);

#endif /* NEX_BIGINT_BIN64_H */

/*
 * brief: 带余除法（截断除法，与 C99 整数除法语义相同）
 * note: 转换包装复用 32 位肢 bin 除法（含 BZ 递归与 Knuth D）
 */
bigint_err_ty bigint_bin64_div_rem(bigint_bin64_ty *quot,
        bigint_bin64_ty *rem, const bigint_bin64_ty *lhs,
        const bigint_bin64_ty *rhs);
