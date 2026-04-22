#include <assert.h>

#include "../include/job_queue.h"
#include "../include/http_request.h"

int main(void) {
    JobQueue queue = {0};
    QueryJob job;

    assert(job_queue_init(&queue, 1) == JOB_QUEUE_ERR_NOT_IMPLEMENTED);
    assert(job_queue_size(&queue) == 0);

    query_job_init(&job);
    assert(job_queue_push(&queue, &job) == JOB_QUEUE_ERR_NOT_IMPLEMENTED);
    assert(job_queue_pop(&queue, &job) == JOB_QUEUE_ERR_NOT_IMPLEMENTED);
    query_job_free(&job);

    job_queue_close(&queue);
    assert(job_queue_is_closed(&queue) == 1);

    job_queue_destroy(&queue);
    return 0;
}
