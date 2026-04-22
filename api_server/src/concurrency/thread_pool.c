#include <stddef.h>
#include <stdlib.h>

#include "../../include/http_request.h"
#include "../../include/thread_pool.h"

/* worker는 큐에서 job을 꺼내고, 연결된 handler로 실제 처리를 위임한다. */
static void *thread_pool_worker_main(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    QueryJob job;

    query_job_init(&job);

    while (job_queue_pop(pool->queue, &job) == JOB_QUEUE_OK) {
        if (pool->handler) {
            pool->handler(&job, pool->handler_context);
        }
        query_job_free(&job);
    }

    query_job_free(&job);
    return NULL;
}

int thread_pool_init(ThreadPool *pool, JobQueue *queue, int thread_count) {
    if (!pool || !queue || thread_count <= 0) {
        return THREAD_POOL_ERR_INVALID_ARG;
    }

    pool->queue = queue;
    /* worker 핸들 배열만 먼저 확보하고 실제 스레드는 start에서 생성한다. */
    pool->threads = (pthread_t *)calloc((size_t)thread_count, sizeof(pthread_t));
    if (!pool->threads) {
        pool->queue = NULL;
        return THREAD_POOL_ERR_NO_MEMORY;
    }

    pool->thread_count = thread_count;
    pool->started = 0;
    pool->handler = NULL;
    pool->handler_context = NULL;

    return THREAD_POOL_OK;
}

int thread_pool_set_handler(ThreadPool *pool,
                            ThreadPoolJobHandler handler,
                            void *context) {
    if (!pool || !handler || pool->started) {
        return THREAD_POOL_ERR_INVALID_ARG;
    }

    /* server-core가 제공하는 job 처리 함수를 저장한다. */
    pool->handler = handler;
    pool->handler_context = context;
    return THREAD_POOL_OK;
}

int thread_pool_start(ThreadPool *pool) {
    int i;

    if (!pool || !pool->queue || pool->thread_count <= 0) {
        return THREAD_POOL_ERR_INVALID_ARG;
    }

    if (pool->started) return THREAD_POOL_OK;

    /* worker를 모두 띄우고, 중간 실패 시 이미 뜬 스레드는 정리한다. */
    for (i = 0; i < pool->thread_count; i++) {
        if (pthread_create(&pool->threads[i],
                           NULL,
                           thread_pool_worker_main,
                           pool) != 0) {
            int joined;

            job_queue_close(pool->queue);
            for (joined = 0; joined < i; joined++) {
                pthread_join(pool->threads[joined], NULL);
            }
            return THREAD_POOL_ERR_START_FAILED;
        }
    }

    pool->started = 1;
    return THREAD_POOL_OK;
}

int thread_pool_stop(ThreadPool *pool) {
    int i;

    if (!pool) return THREAD_POOL_ERR_INVALID_ARG;
    if (!pool->started) return THREAD_POOL_OK;

    /* 큐를 닫아 pop 대기 중인 worker들이 종료 루프를 타게 만든다. */
    job_queue_close(pool->queue);
    for (i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pool->started = 0;
    return THREAD_POOL_OK;
}

void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) return;

    /* destroy 전에 살아 있는 worker가 있으면 안전하게 stop부터 수행한다. */
    if (pool->started) {
        (void)thread_pool_stop(pool);
    }

    free(pool->threads);
    pool->queue = NULL;
    pool->threads = NULL;
    pool->thread_count = 0;
    pool->started = 0;
    pool->handler = NULL;
    pool->handler_context = NULL;
}

int thread_pool_is_started(const ThreadPool *pool) {
    if (!pool) return 0;
    return pool->started;
}
