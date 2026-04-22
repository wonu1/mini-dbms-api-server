#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include "api_types.h"

typedef enum {
    HTTP_REQUEST_OK = 0,
    HTTP_REQUEST_ERR_INVALID_ARG = -1,
    HTTP_REQUEST_ERR_UNSUPPORTED_MEDIA_TYPE = -2,
    HTTP_REQUEST_ERR_BAD_REQUEST = -3,
    HTTP_REQUEST_ERR_NOT_IMPLEMENTED = -4
} HttpRequestStatus;

/* init/free 쌍으로 요청 메모리 소유권을 관리한다. */
void api_query_request_init(ApiQueryRequest *request);
void api_query_request_free(ApiQueryRequest *request);
void query_job_init(QueryJob *job);
void query_job_free(QueryJob *job);

int http_query_is_single_statement(const char *sql);
/*
 * out_request는 api_query_request_init() 이후 전달하는 것을 권장한다.
 * 성공 시 소유권은 호출자에게 넘어가며 api_query_request_free()로 정리한다.
 */
int http_parse_query_request(const char *content_type,
                             const char *body,
                             ApiQueryRequest *out_request);

#endif /* HTTP_REQUEST_H */
