#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../include/http_request.h"
#include "../include/job_queue.h"

/*
 * JobQueue 단위 테스트다.
 * queue 초기화, push/pop, deep copy, close 동작, blocking pop을 확인한다.
 * 실제 HTTP 서버 없이 queue 함수만 직접 호출해서 검증한다.
 */

typedef struct {
    JobQueue *queue;
    int status;
    QueryJob job;
} PopThreadResult;

/* 테스트에서 문자열 복사 책임을 명확하게 하기 위한 헬퍼다. */
static char *dup_text(const char *text) {
    size_t len;
    char *copy;

    if (!text) return NULL;

    len = strlen(text) + 1U;
    copy = (char *)malloc(len);
    assert(copy != NULL);
    memcpy(copy, text, len);
    return copy;
}

/* 큐가 job을 깊은 복사하는지 보기 위해 요청을 직접 채운다. */
static void fill_job(QueryJob *job,
                     int client_fd,
                     const char *sql,
                     const char *request_id) {
    query_job_init(job);
    job->client_fd = client_fd;
    job->request.sql = dup_text(sql);
    job->request.request_id = dup_text(request_id);
}

/* blocking pop 동작을 검증하기 위해 별도 스레드에서 pop을 수행한다. */
static void *pop_thread_main(void *arg) {
    PopThreadResult *result = (PopThreadResult *)arg;

    result->status = job_queue_pop(result->queue, &result->job);
    return NULL;
}

/* 기본 push/pop과 deep copy가 의도대로 동작하는지 확인한다. */
static void test_queue_push_pop_and_copy(void) {
    JobQueue queue;
    QueryJob pushed;
    QueryJob popped;

    assert(job_queue_init(&queue, 2) == JOB_QUEUE_OK);
    assert(job_queue_size(&queue) == 0);

    fill_job(&pushed, 101, "SELECT * FROM users WHERE id = 1;", "req-1");
    assert(job_queue_push(&queue, &pushed) == JOB_QUEUE_OK);
    assert(job_queue_size(&queue) == 1);

    query_job_free(&pushed);

    query_job_init(&popped);
    assert(job_queue_pop(&queue, &popped) == JOB_QUEUE_OK);
    assert(popped.client_fd == 101);
    assert(strcmp(popped.request.sql, "SELECT * FROM users WHERE id = 1;") == 0);
    assert(strcmp(popped.request.request_id, "req-1") == 0);
    assert(job_queue_size(&queue) == 0);

    query_job_free(&popped);
    job_queue_destroy(&queue);
}

/* 큐가 가득 찼을 때와 close 이후의 반환 코드를 검증한다. */
static void test_queue_full_and_close(void) {
    JobQueue queue;
    QueryJob first;
    QueryJob second;
    QueryJob popped;

    assert(job_queue_init(&queue, 1) == JOB_QUEUE_OK);

    fill_job(&first, 201, "INSERT INTO users (name, age, email) VALUES ('a', 20, 'a@example.com');", "req-2");
    fill_job(&second, 202, "INSERT INTO users (name, age, email) VALUES ('b', 21, 'b@example.com');", "req-3");

    assert(job_queue_push(&queue, &first) == JOB_QUEUE_OK);
    assert(job_queue_push(&queue, &second) == JOB_QUEUE_ERR_FULL);

    job_queue_close(&queue);
    assert(job_queue_is_closed(&queue) == 1);
    assert(job_queue_push(&queue, &second) == JOB_QUEUE_ERR_CLOSED);

    query_job_init(&popped);
    assert(job_queue_pop(&queue, &popped) == JOB_QUEUE_OK);
    assert(strcmp(popped.request.request_id, "req-2") == 0);
    query_job_free(&popped);

    query_job_init(&popped);
    assert(job_queue_pop(&queue, &popped) == JOB_QUEUE_ERR_CLOSED);
    query_job_free(&popped);

    query_job_free(&first);
    query_job_free(&second);
    job_queue_destroy(&queue);
}

/* 대기 중인 pop이 새 작업 push로 정상 해제되는지 본다. */
static void test_blocking_pop_unblocks_on_push(void) {
    JobQueue queue;
    PopThreadResult result;
    QueryJob pushed;
    pthread_t thread;

    assert(job_queue_init(&queue, 1) == JOB_QUEUE_OK);

    result.queue = &queue;
    result.status = JOB_QUEUE_ERR_EMPTY;
    query_job_init(&result.job);

    assert(pthread_create(&thread, NULL, pop_thread_main, &result) == 0);

    fill_job(&pushed, 301, "SELECT * FROM users WHERE id = 2;", "req-4");
    assert(job_queue_push(&queue, &pushed) == JOB_QUEUE_OK);
    assert(pthread_join(thread, NULL) == 0);

    assert(result.status == JOB_QUEUE_OK);
    assert(result.job.client_fd == 301);
    assert(strcmp(result.job.request.request_id, "req-4") == 0);

    query_job_free(&pushed);
    query_job_free(&result.job);
    job_queue_destroy(&queue);
}

/* 대기 중인 pop이 queue close로 종료되는지 확인한다. */
static void test_blocking_pop_unblocks_on_close(void) {
    JobQueue queue;
    PopThreadResult result;
    pthread_t thread;

    assert(job_queue_init(&queue, 1) == JOB_QUEUE_OK);

    result.queue = &queue;
    result.status = JOB_QUEUE_ERR_EMPTY;
    query_job_init(&result.job);

    assert(pthread_create(&thread, NULL, pop_thread_main, &result) == 0);
    job_queue_close(&queue);
    assert(pthread_join(thread, NULL) == 0);

    assert(result.status == JOB_QUEUE_ERR_CLOSED);

    query_job_free(&result.job);
    job_queue_destroy(&queue);
}

int main(void) {
    /* Agent C가 맡은 큐 정책을 핵심 시나리오별로 검증한다. */
    test_queue_push_pop_and_copy();
    test_queue_full_and_close();
    test_blocking_pop_unblocks_on_push();
    test_blocking_pop_unblocks_on_close();
    return 0;
}
