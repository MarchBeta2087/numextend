#ifndef NEX_NTT_H
#define NEX_NTT_H

#include <stddef.h>
#include <stdint.h>

/*
 * nex_ntt：数论变换（NTT）核心模块（设计文档 §4.3）。
 *
 * 本模块是 bin / dec 乘法分派共用的只读公共算法模块：只操作系数数组
 * （uint32_t * + 长度），不依赖任何 bigint 类型，无动态分配。
 *
 * 关键约定：
 *   - 模数一律为形如 p = k·2^c + 1 的素数（k 奇），内置表见 ntt_mod_get()，
 *     c 覆盖 21..27。选取模数组合须满足：每个模数 2^c ≥ N，且 Πp > C
 *     （C 为卷积系数上界，见设计文档 §4.3）；
 *   - 变换长度 N = 2^log2n（须 log2n ≤ mod->c）；正变换用 DIF（频域抽取，
 *     Gentleman–Sande）蝶形、逆变换用 DIT（时域抽取，Cooley–Tukey）蝶形，
 *     正逆配对后输入输出均为自然序，无需位反转（设计文档 §4.3）；
 *   - 逆变换末尾乘以 N^-1 mod p 归一化；
 *   - 本模块所有函数无全局可变状态、线程安全（设计文档 §3.3）；
 *   - 传入的 mod 必须已通过 ntt_mod_validate() 校验（内置表天然合法）。
 *
 * 循环卷积配方（调用方自备缓冲，长度 N = 2^log2n）：
 *     ntt_forward(buf_a, log2n, mod);
 *     ntt_forward(buf_b, log2n, mod);
 *     ntt_pointwise_mul(buf_a, buf_a, buf_b, n, mod);
 *     ntt_inverse(buf_a, log2n, mod);   // buf_a 为循环卷积结果
 */

/* NTT 模数：素数 p = k·2^c + 1 及其 2^c 次本原单位根 */
typedef struct {
    uint32_t p;  /* 素数，p = k·2^c + 1 */
    uint32_t k;  /* 奇部，k = (p−1) / 2^c */
    uint32_t c;  /* p−1 中 2 的最大幂指数（2^c || p−1，= 最大变换长度对数） */
    uint32_t w;  /* 2^c 次本原单位根：w^(2^(c−1)) ≡ −1 (mod p) */
} ntt_mod_ty;

/* NTT 模块错误码 */
typedef enum {
    NTT_OK_E = 0,       // 成功
    NTT_ERR_INVALID_E,  // 非法参数（空指针、log2n 超出模数能力等）
    NTT_ERR_MODULUS_E   // 模数校验失败（合数、根阶不符等）
} ntt_err_ty;

/*
 * brief: 内置模数个数（设计文档 §4.3 素数表）
 * return: 内置表长度
 */
size_t ntt_mod_count(void);

/*
 * brief: 取第 idx 个内置模数
 * param: idx  下标，须小于 ntt_mod_count()
 * return: 指向内置表中元素的指针；idx 越界返回 NULL
 */
const ntt_mod_ty *ntt_mod_get(size_t idx);

/*
 * brief: 校验模数合法性（纯函数，无副作用）
 * param: mod  待校验模数（p / k / c / w 四字段）
 * return: 合法返回 NTT_OK_E；合数、k 非奇、c 越界、p−1 ≠ k·2^c 或
 *         根阶不符返回 NTT_ERR_MODULUS_E；mod 为空返回 NTT_ERR_INVALID_E
 * note: 质数性用 Miller-Rabin，基数 {2,3,5,7,11} 对全部 uint32 为确定性
 *       （最小强伪素数 2,152,302,898,747 > 2^32，设计文档 §4.3）；
 *       根阶验证 w^(2^(c−1)) ≡ −1 (mod p) 在 p 为素数时等价于 w 的阶
 *       恰为 2^c。合数模下 Z/mZ 单位群非循环，不存在 2^c 阶根，
 *       radix-2 NTT 无正确性保证，故合数一律不可用
 */
ntt_err_ty ntt_mod_validate(const ntt_mod_ty *mod);

/*
 * brief: 模乘 a·b mod p
 * note: 中间量经 uint64_t（C99 无 128 位扩展，设计文档 §4.3）；a、b
 *       无须小于 p，结果恒在 [0, p)
 */
uint32_t ntt_mod_mul(uint32_t a, uint32_t b, const ntt_mod_ty *mod);

/*
 * brief: 模幂 base^exp mod p（平方-乘）
 */
uint32_t ntt_mod_pow(uint32_t base, uint32_t exp, const ntt_mod_ty *mod);

/*
 * brief: 模逆 a^(p−2) mod p（Fermat 小定理）
 * note: 要求 p 为素数且 a 非零（调用方契约；a 无须小于 p）
 */
uint32_t ntt_mod_inv(uint32_t a, const ntt_mod_ty *mod);

/*
 * brief: 就地正变换（DIF 蝶形；自然序输入 → 位反转输出）
 * param: coeffs  长度 N = 2^log2n 的系数数组（就地改写）
 * param: log2n   变换长度对数，须满足 log2n ≤ mod->c
 */
ntt_err_ty ntt_forward(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod);

/*
 * brief: 就地逆变换（DIT 蝶形；位反转输入 → 自然序输出，含 N^-1 归一化）
 */
ntt_err_ty ntt_inverse(uint32_t *coeffs, uint32_t log2n,
        const ntt_mod_ty *mod);

/*
 * brief: 频域点乘 dst[i] = lhs[i]·rhs[i] mod p（逐元素）
 * note: dst 允许与 lhs / rhs 别名
 */
ntt_err_ty ntt_pointwise_mul(uint32_t *dst, const uint32_t *lhs,
        const uint32_t *rhs, size_t len, const ntt_mod_ty *mod);

/*
 * brief: 单系数 CRT 重构（Garner 递推），恢复精确系数 v（0 ≤ v < Πp）
 * param: residues  各模数下的余数，长度 = mod_count
 * param: mods      模数数组（已通过校验），长度 = mod_count
 * param: mod_count 模数个数；要求 Πp < 2^64（结果以 uint64_t 返回；
 *                 内置表任意两个模数满足，三个则超出，须自备小模数）
 */
uint64_t ntt_crt_reconstruct_one(const uint32_t *residues,
        const ntt_mod_ty *const *mods, size_t mod_count);

#endif /* NEX_NTT_H */
