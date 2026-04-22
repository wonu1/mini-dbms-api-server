#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <stddef.h>

#include "api_types.h"

typedef struct {
    QueryJob *items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int closed;
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
