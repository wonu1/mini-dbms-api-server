#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

#include "job_queue.h"

/*
 * worker가 실제 job 처리 로직을 외부에서 주입받도록 한 콜백이다.
 * ThreadPool은 "thread를 돌리는 일"만 알고, job을 어떻게 처리할지는 server-core가 알려준다.
 */
typedef void (*ThreadPoolJobHandler)(QueryJob *job, void *context);

/*
 * ThreadPool은 여러 worker thread를 묶어서 관리하는 구조체다.
 * worker들은 같은 JobQueue에서 QueryJob을 하나씩 꺼내 handler로 처리한다.
 */
typedef struct {
    /* worker들이 공유하는 작업 queue다. */
    JobQueue *queue;

    /* 생성된 worker 스레드 핸들을 보관해 stop 시 join한다. */
    pthread_t *threads;

    /* worker thread 개수다. */
    int thread_count;

    /* thread pool이 이미 시작됐는지 나타낸다. */
    int started;

    /* server-core가 연결할 실제 요청 처리 함수와 컨텍스트다. */
    ThreadPoolJobHandler handler;

    /* handler가 추가로 필요로 하는 서버 상태 포인터다. */
    void *handler_context;
} ThreadPool;

typedef enum {
    /* thread pool 작업 성공 */
    THREAD_POOL_OK = 0,

    /* NULL 포인터나 thread_count <= 0 같은 잘못된 인자 */
    THREAD_POOL_ERR_INVALID_ARG = -1,

    /* thread 배열 할당 실패 */
    THREAD_POOL_ERR_NO_MEMORY = -2,

    /* pthread_create 실패처럼 worker 시작 중 실패 */
    THREAD_POOL_ERR_START_FAILED = -3
} ThreadPoolStatus;

/* ThreadPool 구조체에 queue와 worker 개수를 연결한다. */
int thread_pool_init(ThreadPool *pool, JobQueue *queue, int thread_count);

/* start 전에 worker 콜백을 연결한다. */
int thread_pool_set_handler(ThreadPool *pool,
                            ThreadPoolJobHandler handler,
                            void *context);

/* worker thread들을 생성해 작업 처리를 시작한다. */
int thread_pool_start(ThreadPool *pool);

/* queue를 닫고 worker들이 끝날 때까지 기다린다. */
int thread_pool_stop(ThreadPool *pool);

/* thread pool 내부 메모리와 상태를 정리한다. */
void thread_pool_destroy(ThreadPool *pool);

/* thread pool이 시작된 상태인지 확인한다. */
int thread_pool_is_started(const ThreadPool *pool);

#endif /* THREAD_POOL_H */
