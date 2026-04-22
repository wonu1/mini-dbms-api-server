#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "job_queue.h"

typedef struct {
    JobQueue *queue;
    int thread_count;
    int started;
} ThreadPool;

typedef enum {
    THREAD_POOL_OK = 0,
    THREAD_POOL_ERR_INVALID_ARG = -1,
    THREAD_POOL_ERR_NOT_IMPLEMENTED = -2
} ThreadPoolStatus;

int thread_pool_init(ThreadPool *pool, JobQueue *queue, int thread_count);
int thread_pool_start(ThreadPool *pool);
int thread_pool_stop(ThreadPool *pool);
void thread_pool_destroy(ThreadPool *pool);
int thread_pool_is_started(const ThreadPool *pool);

#endif /* THREAD_POOL_H */
