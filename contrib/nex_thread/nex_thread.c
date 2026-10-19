/*
 * nex_thread.c：contrib 层最小线程池（pthread / Win32）。
 *
 * 实现要点：
 *   - 调用方拥有的池对象，无全局状态；池内互斥量只保护调度元数据，
 *     job 执行期间不持锁；
 *   - run 把 [0, njobs) 的唯一 index 分发给工作线程，阻塞至全部完成；
 *   - 确定性由调用方保证：job 必须相互独立，线程数不改变结果；
 *   - Win32 使用 Vista+ 的 CONDITION_VARIABLE（MSVC / MinGW 均可用）。
 */

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "nex_thread.h"

#include <stdlib.h>

struct nex_thread_pool {
    unsigned nthreads;
#if defined(_WIN32)
    HANDLE *threads;
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE work_cv;
    CONDITION_VARIABLE done_cv;
#else
    pthread_t *threads;
    pthread_mutex_t mutex;
    pthread_cond_t work_cv;
    pthread_cond_t done_cv;
#endif
    size_t njobs;
    size_t next;
    unsigned generation;
    unsigned busy;
    int shutdown;
    nex_thread_job_fn fn;
    void *ctx;
};

#if defined(_WIN32)
#define POOL_LOCK(p) EnterCriticalSection(&(p)->mutex)
#define POOL_UNLOCK(p) LeaveCriticalSection(&(p)->mutex)
#define POOL_WAIT_WORK(p) \
    (void)SleepConditionVariableCS(&(p)->work_cv, &(p)->mutex, INFINITE)
#define POOL_WAIT_DONE(p) \
    (void)SleepConditionVariableCS(&(p)->done_cv, &(p)->mutex, INFINITE)
#define POOL_WAKE_ALL(p) WakeAllConditionVariable(&(p)->work_cv)
#define POOL_WAKE_DONE(p) WakeConditionVariable(&(p)->done_cv)
#else
#define POOL_LOCK(p) (void)pthread_mutex_lock(&(p)->mutex)
#define POOL_UNLOCK(p) (void)pthread_mutex_unlock(&(p)->mutex)
#define POOL_WAIT_WORK(p) (void)pthread_cond_wait(&(p)->work_cv, &(p)->mutex)
#define POOL_WAIT_DONE(p) (void)pthread_cond_wait(&(p)->done_cv, &(p)->mutex)
#define POOL_WAKE_ALL(p) (void)pthread_cond_broadcast(&(p)->work_cv)
#define POOL_WAKE_DONE(p) (void)pthread_cond_signal(&(p)->done_cv)
#endif

static unsigned hw_threads(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (unsigned)si.dwNumberOfProcessors;
#else
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (unsigned)n : 1U;
#endif
}

static void pool_worker_loop(nex_thread_pool_ty *p)
{
    unsigned seen = 0U;
    for (;;) {
        POOL_LOCK(p);
        while (!p->shutdown && (p->generation == seen)) {
            POOL_WAIT_WORK(p);
        }
        if (p->shutdown) {
            POOL_UNLOCK(p);
            break;
        }
        seen = p->generation;
        nex_thread_job_fn fn = p->fn;
        void *ctx = p->ctx;
        for (;;) {
            if (p->next >= p->njobs) {
                break;
            }
            const size_t idx = p->next++;
            POOL_UNLOCK(p);
            fn(ctx, idx);
            POOL_LOCK(p);
        }
        p->busy--;
        if (p->busy == 0U) {
            POOL_WAKE_DONE(p);
        }
        POOL_UNLOCK(p);
    }
}

#if defined(_WIN32)
static DWORD WINAPI pool_thread_main(LPVOID arg)
{
    pool_worker_loop((nex_thread_pool_ty *)arg);
    return 0U;
}
#else
static void *pool_thread_main(void *arg)
{
    pool_worker_loop((nex_thread_pool_ty *)arg);
    return NULL;
}
#endif

nex_thread_pool_ty *nex_thread_pool_create(unsigned threads)
{
    unsigned n = threads;
    if (n == 0U) {
        n = hw_threads();
    }
    if (n == 0U) {
        n = 1U;
    }
    if (n > 64U) {
        n = 64U;
    }

    nex_thread_pool_ty *p = (nex_thread_pool_ty *)calloc(1U, sizeof(*p));
    if (p == NULL) {
        return NULL;
    }
    p->nthreads = n;
    p->threads = calloc((size_t)n, sizeof(*p->threads));
    if (p->threads == NULL) {
        free(p);
        return NULL;
    }

#if defined(_WIN32)
    InitializeCriticalSection(&p->mutex);
    InitializeConditionVariable(&p->work_cv);
    InitializeConditionVariable(&p->done_cv);
#else
    if ((pthread_mutex_init(&p->mutex, NULL) != 0)
            || (pthread_cond_init(&p->work_cv, NULL) != 0)
            || (pthread_cond_init(&p->done_cv, NULL) != 0)) {
        free(p->threads);
        free(p);
        return NULL;
    }
#endif

    unsigned made = 0U;
    for (; made < n; made++) {
#if defined(_WIN32)
        p->threads[made] = CreateThread(NULL, 0U, pool_thread_main, p, 0U,
                NULL);
        if (p->threads[made] == NULL) {
            break;
        }
#else
        if (pthread_create(&p->threads[made], NULL, pool_thread_main, p) != 0) {
            break;
        }
#endif
    }
    if (made != n) {
        /* 部分创建失败：先停已创建的线程，再回收 */
        POOL_LOCK(p);
        p->shutdown = 1;
        p->generation++;
        POOL_WAKE_ALL(p);
        POOL_UNLOCK(p);
        for (unsigned i = 0U; i < made; i++) {
#if defined(_WIN32)
            (void)WaitForSingleObject(p->threads[i], INFINITE);
            (void)CloseHandle(p->threads[i]);
#else
            (void)pthread_join(p->threads[i], NULL);
#endif
        }
#if defined(_WIN32)
        DeleteCriticalSection(&p->mutex);
#else
        (void)pthread_cond_destroy(&p->done_cv);
        (void)pthread_cond_destroy(&p->work_cv);
        (void)pthread_mutex_destroy(&p->mutex);
#endif
        free(p->threads);
        free(p);
        return NULL;
    }
    return p;
}

void nex_thread_pool_destroy(nex_thread_pool_ty *pool)
{
    if (pool == NULL) {
        return;
    }
    POOL_LOCK(pool);
    pool->shutdown = 1;
    pool->generation++;
    POOL_WAKE_ALL(pool);
    POOL_UNLOCK(pool);
    for (unsigned i = 0U; i < pool->nthreads; i++) {
#if defined(_WIN32)
        (void)WaitForSingleObject(pool->threads[i], INFINITE);
        (void)CloseHandle(pool->threads[i]);
#else
        (void)pthread_join(pool->threads[i], NULL);
#endif
    }
#if defined(_WIN32)
    DeleteCriticalSection(&pool->mutex);
#else
    (void)pthread_cond_destroy(&pool->done_cv);
    (void)pthread_cond_destroy(&pool->work_cv);
    (void)pthread_mutex_destroy(&pool->mutex);
#endif
    free(pool->threads);
    free(pool);
}

unsigned nex_thread_pool_count(const nex_thread_pool_ty *pool)
{
    return (pool != NULL) ? pool->nthreads : 0U;
}

void nex_thread_pool_run(nex_thread_pool_ty *pool, size_t njobs,
        nex_thread_job_fn fn, void *ctx)
{
    if ((njobs == 0U) || (fn == NULL)) {
        return;
    }
    if (pool == NULL) {
        for (size_t i = 0U; i < njobs; i++) {
            fn(ctx, i);
        }
        return;
    }
    POOL_LOCK(pool);
    pool->fn = fn;
    pool->ctx = ctx;
    pool->njobs = njobs;
    pool->next = 0U;
    pool->busy = pool->nthreads;
    pool->generation++;
    POOL_WAKE_ALL(pool);
    while (pool->busy != 0U) {
        POOL_WAIT_DONE(pool);
    }
    POOL_UNLOCK(pool);
}
