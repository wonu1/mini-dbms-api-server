#include <stdlib.h>
#include <string.h>

#include "../../include/job_queue.h"
#include "../../include/http_request.h"

/*
 * 이 파일은 worker들이 사용할 작업 queue를 구현한다.
 * HTTP server thread가 QueryJob을 넣고, worker thread들이 하나씩 꺼내 처리한다.
 * 여러 thread가 동시에 접근하므로 mutex로 상태 변경을 보호하고,
 * condition variable로 "일이 들어왔다"는 신호를 worker에게 보낸다.
 */

/* QueryJob 내부 문자열까지 안전하게 복사하기 위한 작은 유틸리티다. */
/* 문자열 하나를 새 메모리에 복사한다. QueryJob deep copy에서 사용한다. */
static char *job_queue_dup_string(const char *src) {
    size_t len;
    char *copy;

    if (!src) return NULL;

    len = strlen(src) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;

    memcpy(copy, src, len);
    return copy;
}

/* 큐는 job의 소유권을 가져야 하므로 얕은 복사가 아니라 깊은 복사를 사용한다. */
/* src QueryJob의 fd/sql/request_id를 dst 슬롯으로 깊은 복사한다. */
static int job_queue_copy_job(QueryJob *dst, const QueryJob *src) {
    char *sql_copy = NULL;
    char *request_id_copy = NULL;

    if (!dst || !src) return JOB_QUEUE_ERR_INVALID_ARG;

    if (src->request.sql) {
        sql_copy = job_queue_dup_string(src->request.sql);
        if (!sql_copy) return JOB_QUEUE_ERR_NO_MEMORY;
    }

    if (src->request.request_id) {
        request_id_copy = job_queue_dup_string(src->request.request_id);
        if (!request_id_copy) {
            free(sql_copy);
            return JOB_QUEUE_ERR_NO_MEMORY;
        }
    }

    query_job_free(dst);
    dst->client_fd = src->client_fd;
    dst->request.sql = sql_copy;
    dst->request.request_id = request_id_copy;
    return JOB_QUEUE_OK;
}

/* JobQueue 저장 배열, 원형 queue 인덱스, mutex/condition variable을 초기화한다. */
int job_queue_init(JobQueue *queue, size_t capacity) {
    size_t i;

    if (!queue || capacity == 0) return JOB_QUEUE_ERR_INVALID_ARG;

    /* 링 버퍼 슬롯을 먼저 만들고 각 슬롯을 빈 QueryJob 상태로 맞춘다. */
    queue->items = (QueryJob *)calloc(capacity, sizeof(QueryJob));
    if (!queue->items) return JOB_QUEUE_ERR_NO_MEMORY;

    for (i = 0; i < capacity; i++) {
        query_job_init(&queue->items[i]);
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->closed = 0;
    queue->sync_ready = 0;

    /* 큐 동기화에 필요한 mutex/condvar를 한 번에 준비한다. */
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->items);
        queue->items = NULL;
        return JOB_QUEUE_ERR_NO_MEMORY;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        queue->items = NULL;
        return JOB_QUEUE_ERR_NO_MEMORY;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->items);
        queue->items = NULL;
        return JOB_QUEUE_ERR_NO_MEMORY;
    }

    queue->sync_ready = 1;
    return JOB_QUEUE_OK;
}

/* producer가 새 QueryJob을 queue tail 위치에 넣는다. 꽉 차면 즉시 FULL을 반환한다. */
int job_queue_push(JobQueue *queue, const QueryJob *job) {
    int status;

    if (!queue || !job) return JOB_QUEUE_ERR_INVALID_ARG;
    if (!queue->sync_ready) return JOB_QUEUE_ERR_INVALID_ARG;

    /* push는 tail/count를 바꾸므로 큐 전체를 잠그고 처리한다. */
    pthread_mutex_lock(&queue->mutex);

    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return JOB_QUEUE_ERR_CLOSED;
    }

    /* 현재 정책은 대기하지 않고 즉시 FULL을 반환하는 방식이다. */
    if (queue->count == queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return JOB_QUEUE_ERR_FULL;
    }

    status = job_queue_copy_job(&queue->items[queue->tail], job);
    if (status != JOB_QUEUE_OK) {
        pthread_mutex_unlock(&queue->mutex);
        return status;
    }

    queue->tail = (queue->tail + 1U) % queue->capacity;
    queue->count++;
    /* 비어 있던 큐에 작업이 들어왔을 수 있으니 worker 하나를 깨운다. */
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return JOB_QUEUE_OK;
}

/* worker가 queue head 위치에서 QueryJob을 꺼낸다. 비어 있으면 작업이 올 때까지 기다린다. */
int job_queue_pop(JobQueue *queue, QueryJob *out_job) {
    if (!queue || !out_job) return JOB_QUEUE_ERR_INVALID_ARG;
    if (!queue->sync_ready) return JOB_QUEUE_ERR_INVALID_ARG;

    /* pop도 head/count를 건드리므로 같은 mutex로 보호한다. */
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0 && !queue->closed) {
        /* 큐가 비어 있으면 busy loop 대신 조건변수에서 잠든다. */
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        /* close 이후 더 꺼낼 작업이 없으면 worker 종료 신호로 해석한다. */
        return queue->closed ? JOB_QUEUE_ERR_CLOSED : JOB_QUEUE_ERR_EMPTY;
    }

    /* 큐가 가진 job 소유권을 caller 쪽으로 넘기고 슬롯은 다시 비운다. */
    query_job_free(out_job);
    *out_job = queue->items[queue->head];
    query_job_init(&queue->items[queue->head]);

    queue->head = (queue->head + 1U) % queue->capacity;
    queue->count--;
    /* 자리가 생겼으니 필요한 경우 producer가 다시 push할 수 있다. */
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return JOB_QUEUE_OK;
}

/* queue를 닫고, pop/push에서 기다릴 수 있는 thread들을 모두 깨운다. */
void job_queue_close(JobQueue *queue) {
    if (!queue) return;

    if (!queue->sync_ready) {
        queue->closed = 1;
        return;
    }

    /* close는 대기 중인 모든 스레드가 종료 경로를 볼 수 있게 broadcast한다. */
    pthread_mutex_lock(&queue->mutex);
    queue->closed = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

/* queue에 남은 job과 동기화 객체, 저장 배열을 모두 정리한다. */
void job_queue_destroy(JobQueue *queue) {
    size_t i;
    int sync_ready;

    if (!queue) return;

    sync_ready = queue->sync_ready;
    if (sync_ready) {
        pthread_mutex_lock(&queue->mutex);
    }

    /* 큐에 남은 job까지 모두 정리해 메모리 누수를 막는다. */
    for (i = 0; i < queue->capacity; i++) {
        if (queue->items) query_job_free(&queue->items[i]);
    }

    free(queue->items);
    queue->items = NULL;
    queue->capacity = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->closed = 0;
    queue->sync_ready = 0;

    if (sync_ready) {
        pthread_mutex_unlock(&queue->mutex);
        pthread_cond_destroy(&queue->not_empty);
        pthread_cond_destroy(&queue->not_full);
        pthread_mutex_destroy(&queue->mutex);
    }
}

/* 현재 queue에 들어 있는 job 개수를 thread-safe하게 읽는다. */
size_t job_queue_size(const JobQueue *queue) {
    size_t size = 0;

    if (!queue) return 0;
    if (!queue->sync_ready) return queue->count;

    /* 읽기만 해도 count는 공유 상태라 잠금 후 조회한다. */
    pthread_mutex_lock((pthread_mutex_t *)&queue->mutex);
    size = queue->count;
    pthread_mutex_unlock((pthread_mutex_t *)&queue->mutex);
    return size;
}

/* queue가 닫혔는지 thread-safe하게 읽는다. */
int job_queue_is_closed(const JobQueue *queue) {
    int closed = 0;

    if (!queue) return 0;
    if (!queue->sync_ready) return queue->closed;

    /* closed 플래그도 여러 스레드가 보므로 잠금 후 읽는다. */
    pthread_mutex_lock((pthread_mutex_t *)&queue->mutex);
    closed = queue->closed;
    pthread_mutex_unlock((pthread_mutex_t *)&queue->mutex);
    return closed;
}
