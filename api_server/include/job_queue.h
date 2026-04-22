#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <stddef.h>
#include <pthread.h>

#include "api_types.h"

typedef struct {
    QueryJob *items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int closed;
    /* 동기화 초기화 성공 여부를 기록해 destroy 경로를 단순하게 만든다. */
    int sync_ready;
    /* 생산자/소비자가 동시에 큐 상태를 망가뜨리지 않도록 보호한다. */
    pthread_mutex_t mutex;
    /* 비어 있던 큐에 새 작업이 들어오면 worker를 깨운다. */
    pthread_cond_t not_empty;
    /* 큐가 가득 찼다가 자리가 생기면 producer를 깨운다. */
    pthread_cond_t not_full;
} JobQueue;

typedef enum {
    JOB_QUEUE_OK = 0,
    JOB_QUEUE_ERR_INVALID_ARG = -1,
    JOB_QUEUE_ERR_NO_MEMORY = -2,
    JOB_QUEUE_ERR_FULL = 1,
    JOB_QUEUE_ERR_EMPTY = 2,
    JOB_QUEUE_ERR_CLOSED = 3,
    JOB_QUEUE_ERR_NOT_IMPLEMENTED = 4
} JobQueueStatus;

int job_queue_init(JobQueue *queue, size_t capacity);
int job_queue_push(JobQueue *queue, const QueryJob *job);
/* pop 성공 시 out_job의 소유권은 호출자에게 넘어가며 query_job_free()로 정리한다. */
int job_queue_pop(JobQueue *queue, QueryJob *out_job);
void job_queue_close(JobQueue *queue);
void job_queue_destroy(JobQueue *queue);
size_t job_queue_size(const JobQueue *queue);
int job_queue_is_closed(const JobQueue *queue);

#endif /* JOB_QUEUE_H */
