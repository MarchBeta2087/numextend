#ifndef NEX_BIGINT_COMMON_H
#define NEX_BIGINT_COMMON_H

#include <stddef.h>
#include <stdint.h>

/*
 * bigint 层共享定义：符号枚举、错误码枚举、乘法方法选择类型。
 * 供 bigint_bin 与 bigint_dec 两个子模块共用（设计文档 §3.1、§4.2.4）。
 */

/* 三标志符号：零 / 正 / 负 */
typedef enum {
    BIGINT_SIGN_ZERO_E = 0,  // 零
    BIGINT_SIGN_POS_E,       // 正
    BIGINT_SIGN_NEG_E        // 负
} bigint_sign_ty;

/* bigint 层统一错误码，bin / dec 共用 */
typedef enum {
    BIGINT_OK_E = 0,          // 成功
    BIGINT_ERR_OOM_E,         // 内存分配失败
    BIGINT_ERR_INVALID_E,     // 非法参数（空指针、零长度、非法进制等）
    BIGINT_ERR_DIV_ZERO_E,    // 除数为零
    BIGINT_ERR_OVERFLOW_E,    // 结果超出目标类型表示范围（如转 uint64_t 溢出）
    BIGINT_ERR_PARSE_E,       // 字符串解析失败
    BIGINT_ERR_UNSUPPORTED_E  // 功能已预留但当前版本未实现（如未落地的乘法方法）
} bigint_err_ty;

/*
 * 乘法算法标签。编号分段：0 为 AUTO，1xxx 为亚二次分治类，2xxx 为变换类，
 * 3xxx 为 Schönhage-Strassen 类；新增方法按族续号，不复用已占用编号。
 */
typedef enum {
    BIGINT_MUL_AUTO_E = 0,                    // 按肢数阈值自动分派（默认）
    BIGINT_MUL_SCHOOLBOOK_E = 1001,           // 朴素 O(n^2) 乘法
    BIGINT_MUL_KARATSUBA_E = 1002,            // Karatsuba 分治
    BIGINT_MUL_TOOM_COOK_E = 1003,            // v1 未实现 → UNSUPPORTED
    BIGINT_MUL_FLOAT_COMPLEX_FFT_E = 2001,    // v1 未实现 → UNSUPPORTED
    BIGINT_MUL_MULTI_MODULI_CRT_NTT_E = 2002, // v1 未实现 → UNSUPPORTED
    BIGINT_MUL_SCHONHAGE_STRASSEN_E = 3001    // v1 未实现 → UNSUPPORTED
} bigint_mul_algo_ty;

/* 与 bigint_mul_algo_ty 对应的参数；不适用时忽略（C99 无空结构体，故含 reserved） */
typedef union {
    struct { uint32_t reserved; } schoolbook;             // 无参数
    struct { size_t cutoff; } karatsuba;                  // 切换阈值（肢数），0 = 库默认
    struct { uint32_t k; size_t cutoff; } toom_cook;      // Toom-k 的 k 与阈值
    struct { uint32_t chunk_bits; } float_complex_fft;    // 分节位数，0 = 默认 8
    struct { uint32_t mod_count; } multi_moduli_crt_ntt;  // 模数个数，0 = 库默认
    struct { uint32_t reserved; } schonhage_strassen;     // 无参数
} bigint_mul_params_ty;

/* 乘法方法选择：算法标签 + 对应参数 */
typedef struct {
    bigint_mul_algo_ty algo;     // 算法标签
    bigint_mul_params_ty params; // 与 algo 对应的参数；不适用时忽略
} bigint_mul_method_ty;

#endif /* NEX_BIGINT_COMMON_H */
