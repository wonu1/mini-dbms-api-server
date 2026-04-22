#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

#include "job_queue.h"

/* worker가 실제 job 처리 로직을 외부에서 주입받도록 한 콜백이다. */
typedef void (*ThreadPoolJobHandler)(QueryJob *job, void *context);

typedef struct {
    JobQueue *queue;
    /* 생성된 worker 스레드 핸들을 보관해 stop 시 join한다. */
    pthread_t *threads;
    int thread_count;
    int started;
    /* server-core가 연결할 실제 요청 처리 함수와 컨텍스트다. */
    ThreadPoolJobHandler handler;
    void *handler_context;
} ThreadPool;

typedef enum {
    THREAD_POOL_OK = 0,
    THREAD_POOL_ERR_INVALID_ARG = -1,
    THREAD_POOL_ERR_NO_MEMORY = -2,
    THREAD_POOL_ERR_START_FAILED = -3
} ThreadPoolStatus;

int thread_pool_init(ThreadPool *pool, JobQueue *queue, int thread_count);
/* start 전에 worker 콜백을 연결한다. */
int thread_pool_set_handler(ThreadPool *pool,
                            ThreadPoolJobHandler handler,
                            void *context);
int thread_pool_start(ThreadPool *pool);
int thread_pool_stop(ThreadPool *pool);
void thread_pool_destroy(ThreadPool *pool);
int thread_pool_is_started(const ThreadPool *pool);

#endif /* THREAD_POOL_H */
