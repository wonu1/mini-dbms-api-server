#include "../../include/thread_pool.h"

int thread_pool_init(ThreadPool *pool, JobQueue *queue, int thread_count) {
    if (!pool || !queue || thread_count <= 0) {
        return THREAD_POOL_ERR_INVALID_ARG;
    }

    pool->queue = queue;
    pool->thread_count = thread_count;
    pool->started = 0;

    /* TODO: Prepare worker metadata for startup. */
    return THREAD_POOL_ERR_NOT_IMPLEMENTED;
}

int thread_pool_start(ThreadPool *pool) {
    if (!pool || !pool->queue || pool->thread_count <= 0) {
        return THREAD_POOL_ERR_INVALID_ARG;
    }

    /* TODO: Create workers and connect the execution loop. */
    return THREAD_POOL_ERR_NOT_IMPLEMENTED;
}

int thread_pool_stop(ThreadPool *pool) {
    if (!pool) return THREAD_POOL_ERR_INVALID_ARG;

    /* TODO: Implement graceful worker shutdown. */
    return THREAD_POOL_ERR_NOT_IMPLEMENTED;
}

void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) return;

    pool->queue = NULL;
    pool->thread_count = 0;
    pool->started = 0;
}

int thread_pool_is_started(const ThreadPool *pool) {
    if (!pool) return 0;
    return pool->started;
}
