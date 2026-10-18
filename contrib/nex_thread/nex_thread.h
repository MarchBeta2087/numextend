#ifndef NEX_THREAD_H
#define NEX_THREAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nex_thread：contrib 层的跨平台线程池（pthread / Win32）。
 *
 * 设计边界（TODO §F）：
 *   - 不进入核心 libnex、不进 nex_modules.txt / amalgam / 子集闭包；
 *   - 调用方驱动：池由调用方创建/销毁，库内无全局状态、无锁；
 *   - 每个 job 由唯一 index 标识；job 之间必须相互独立（写不同输出），
 *     执行顺序与线程数不影响结果；
 *   - 错误传播由调用方负责（job 把错误写入自己的上下文）。
 *
 * 构建：-DNEX_ENABLE_THREAD=ON（默认 OFF），仅完整构建。
 */

typedef struct nex_thread_pool nex_thread_pool_ty;

/* job 函数：ctx 为调用方上下文，index ∈ [0, njobs) */
typedef void (*nex_thread_job_fn)(void *ctx, size_t index);

/*
 * brief: 创建线程池
 * param: threads 工作线程数；0 表示按硬件并发数（上限 64）
 * return: 成功返回池；内存不足/线程创建失败返回 NULL
 */
nex_thread_pool_ty *nex_thread_pool_create(unsigned threads);

/*
 * brief: 销毁线程池（等待并回收全部工作线程）
 */
void nex_thread_pool_destroy(nex_thread_pool_ty *pool);

/*
 * brief: 工作线程数
 */
unsigned nex_thread_pool_count(const nex_thread_pool_ty *pool);

/*
 * brief: 执行 njobs 个独立 job（阻塞至全部完成）
 * param: pool  线程池；为 NULL 时在当前线程顺序执行（便于对照/降级）
 * param: njobs job 数；为 0 直接返回
 * param: fn    job 函数；为 NULL 直接返回
 * param: ctx   传给 fn 的上下文
 * note: 同一池上的 run 调用由调用方串行化；不同池可并发使用
 */
void nex_thread_pool_run(nex_thread_pool_ty *pool, size_t njobs,
        nex_thread_job_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NEX_THREAD_H */
