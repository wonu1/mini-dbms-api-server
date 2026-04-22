#include <stddef.h>
#include <stdlib.h>

#include "../../include/http_request.h"
#include "../../include/thread_pool.h"

/*
 * 이 파일은 worker thread 여러 개를 만들고 멈추는 일을 담당한다.
 * ThreadPool 자체는 SQL을 직접 실행하지 않는다.
 * 대신 server-core가 등록한 handler 콜백에게 QueryJob 처리를 맡긴다.
 */

/* worker는 큐에서 job을 꺼내고, 연결된 handler로 실제 처리를 위임한다. */
/* worker thread의 main 함수다. queue에서 job을 반복해서 꺼내 handler로 넘긴다. */
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

/* ThreadPool 구조체를 초기화하고 worker thread 핸들 배열을 준비한다. */
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

/* worker가 job을 꺼냈을 때 호출할 실제 처리 함수(handler)를 등록한다. */
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

/* 설정된 thread_count만큼 worker thread를 생성한다. */
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

/* queue를 닫아 worker loop를 끝내고 모든 worker thread를 join한다. */
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

/* thread pool이 가진 thread 배열과 상태를 정리한다. */
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

/* thread pool이 현재 시작된 상태인지 반환한다. */
int thread_pool_is_started(const ThreadPool *pool) {
    if (!pool) return 0;
    return pool->started;
}
