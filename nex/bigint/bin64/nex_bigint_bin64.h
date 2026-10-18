#ifndef NEX_BIGINT_BIN64_H
#define NEX_BIGINT_BIN64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nex/bigint/nex_bigint_common.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * 本模块提供加/减/乘/除、字符串 I/O（进制 2..36）；与 32 位肢
 * bigint_bin 及 bigint_dec 的互转见 nex_bigint_conv.h。
 * 除法为原生 64 位肢实现（Knuth Algorithm D）；超大输入（除数 ≥ 1024 肢
 * 且被除数 ≥ 2 倍除数）转 32 位肢 bin 走 Burnikel-Ziegler。
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

/*
 * brief: 128/64 除法 (hi·2^64 + lo) / d
 * param: hi 高 64 位；要求 hi < d（保证商恰为 64 位）
 * param: lo 低 64 位
 * param: d 除数（非零）
 * param: rem 传出余数；可为 NULL（不接收）
 * return: 商
 * note: 内部函数，不属公开 API；GCC/Clang 走 __int128，其余走
 *       以 2^32 为基的专用 Knuth D（NEX_BIN64_FORCE_PORTABLE 可强制）
 */
uint64_t nex_u128_divmod(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem);

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

/*
 * brief: 以 uint64_t 赋值
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 */
bigint_err_ty bigint_bin64_from_u64(bigint_bin64_ty *val, uint64_t value);

/*
 * brief: 转为 uint64_t
 * return: 成功返回 BIGINT_OK_E；负数或超出范围返回 BIGINT_ERR_OVERFLOW_E
 *         （out 不被修改）
 */
bigint_err_ty bigint_bin64_to_u64(const bigint_bin64_ty *val, uint64_t *out);

/*
 * brief: 从字符串解析，支持进制 2..36（数字 0-9a-z，大小写均可）
 * param: val 目标对象（已初始化）
 * param: str 源字符串，可选前导 '-'；base 为 16 时允许 "0x"/"0X" 前缀，
 *        base 为 2 时允许 "0b"/"0B" 前缀；不跳过空白
 * param: base 进制，取值 2..36
 * param: end 若非 NULL，返回首个未消费字符位置
 * return: 成功返回 BIGINT_OK_E；首字符即非法（无可消费前缀）返回
 *         BIGINT_ERR_PARSE_E 且 val 不变；base 越界返回 BIGINT_ERR_INVALID_E；
 *         内存不足返回 BIGINT_ERR_OOM_E（val 不变）
 * note: 部分消费容错——尽可能多地消费合法数字（设计文档 §11）
 */
bigint_err_ty bigint_bin64_from_str(bigint_bin64_ty *val, const char *str,
        uint32_t base, const char **end);

/*
 * brief: 转为字符串，支持进制 2..36（数字 0-9a-z 小写；负数带 '-' 前缀）
 * param: val 源对象
 * param: base 进制，取值 2..36
 * param: buf 输出缓冲区；可为 NULL 仅查询所需长度
 * param: buf_len 缓冲区长度
 * param: needed 若非 NULL，传出含 '\0' 的所需长度
 * return: 成功返回 BIGINT_OK_E；buf 不足返回 BIGINT_ERR_OVERFLOW_E
 *         （buf 内容未定义，needed 仍被写出）；base 越界返回
 *         BIGINT_ERR_INVALID_E
 * note: 2 的幂进制走 O(n) 位切片，其余进制按 uint64 分块反复除法
 */
bigint_err_ty bigint_bin64_to_str(const bigint_bin64_ty *val, uint32_t base,
        char *buf, size_t buf_len, size_t *needed);

/*
 * brief: 比较两个大整数
 * return: lhs < rhs 返回负数，相等返回 0，lhs > rhs 返回正数
 */
int bigint_bin64_cmp(const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);

/*
 * brief: 判断是否为零
 */
bool bigint_bin64_is_zero(const bigint_bin64_ty *val);

/* ------------------------------------------------------------------ */
/* 加减乘                                                               */
/* ------------------------------------------------------------------ */

/*
 * brief: 加法 dst = lhs + rhs（允许 dst 与任一操作数别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 */
bigint_err_ty bigint_bin64_add(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);

/*
 * brief: 减法 dst = lhs − rhs（允许 dst 与任一操作数别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 */
bigint_err_ty bigint_bin64_sub(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);

/*
 * brief: 乘法 dst = lhs × rhs（schoolbook + Karatsuba，允许别名）
 * return: 成功返回 BIGINT_OK_E；内存不足返回 BIGINT_ERR_OOM_E
 */
bigint_err_ty bigint_bin64_mul(bigint_bin64_ty *dst,
        const bigint_bin64_ty *lhs, const bigint_bin64_ty *rhs);

/*
 * brief: 带余除法（截断除法，与 C99 整数除法语义相同）
 * param: quot 商，可为 NULL 表示不接收
 * param: rem 余数，可为 NULL 表示不接收；符号与被除数一致
 * return: 除数为零返回 BIGINT_ERR_DIV_ZERO_E（quot / rem 不被修改）；
 *         内存不足返回 BIGINT_ERR_OOM_E；成功返回 BIGINT_OK_E
 * note: 原生 64 位肢 Knuth Algorithm D；quot / rem 允许与 lhs / rhs 别名
 */
bigint_err_ty bigint_bin64_div_rem(bigint_bin64_ty *quot,
        bigint_bin64_ty *rem, const bigint_bin64_ty *lhs,
        const bigint_bin64_ty *rhs);

#ifdef __cplusplus
}
#endif

#endif /* NEX_BIGINT_BIN64_H */
