#include <stdlib.h>

#include "../../include/http_request.h"

void api_query_request_init(ApiQueryRequest *request) {
    if (!request) return;

    request->sql = NULL;
    request->request_id = NULL;
}

void api_query_request_free(ApiQueryRequest *request) {
    if (!request) return;

    free(request->sql);
    free(request->request_id);
    api_query_request_init(request);
}

void query_job_init(QueryJob *job) {
    if (!job) return;

    job->client_fd = -1;
    api_query_request_init(&job->request);
}

void query_job_free(QueryJob *job) {
    if (!job) return;

    api_query_request_free(&job->request);
    job->client_fd = -1;
}

int http_query_is_single_statement(const char *sql) {
    (void)sql;

    /* TODO: Implement the one-statement-per-request rule. */
    return 0;
}

int http_parse_query_request(const char *content_type,
                             const char *body,
                             ApiQueryRequest *out_request) {
    if (!content_type || !body || !out_request) {
        return HTTP_REQUEST_ERR_INVALID_ARG;
    }

    api_query_request_free(out_request);

    /* TODO: Parse application/json and extract sql/request_id. */
    return HTTP_REQUEST_ERR_NOT_IMPLEMENTED;
}
