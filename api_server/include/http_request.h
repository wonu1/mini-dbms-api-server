#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include "api_types.h"

/*
 * http_request 모듈의 반환 코드다.
 * 함수가 성공했는지, 실패했다면 왜 실패했는지를 숫자로 알려준다.
 */
typedef enum {
    /* 요청 파싱이 성공했다. */
    HTTP_REQUEST_OK = 0,

    /* 함수에 NULL 포인터처럼 잘못된 인자가 들어왔다. */
    HTTP_REQUEST_ERR_INVALID_ARG = -1,

    /* Content-Type이 application/json이 아니다. */
    HTTP_REQUEST_ERR_UNSUPPORTED_MEDIA_TYPE = -2,

    /* JSON 문법이 틀렸거나 sql 필드가 없거나 SQL이 한 문장이 아니다. */
    HTTP_REQUEST_ERR_BAD_REQUEST = -3,

    /* 예전 스텁 단계의 값이다. 현재 구현에서는 정상 흐름에서 쓰지 않는다. */
    HTTP_REQUEST_ERR_NOT_IMPLEMENTED = -4
} HttpRequestStatus;

/*
 * init/free 쌍으로 요청 메모리 소유권을 관리한다.
 *
 * C에는 자동으로 문자열 메모리를 정리해주는 기능이 없으므로,
 * malloc된 sql/request_id를 누가 free할지 규칙을 정해야 한다.
 * 이 모듈에서는 성공한 ApiQueryRequest는 호출자가 free하는 규칙을 쓴다.
 */
void api_query_request_init(ApiQueryRequest *request);
void api_query_request_free(ApiQueryRequest *request);

/*
 * QueryJob도 내부에 ApiQueryRequest를 들고 있으므로 init/free가 필요하다.
 * job을 만들 때는 query_job_init(), 다 쓴 뒤에는 query_job_free()를 호출한다.
 */
void query_job_init(QueryJob *job);
void query_job_free(QueryJob *job);

/*
 * SQL 문자열이 한 문장인지 확인한다.
 *
 * 허용:
 *   SELECT * FROM users
 *   SELECT * FROM users;
 *
 * 거부:
 *   SELECT * FROM users; SELECT * FROM users
 *
 * 반환값은 C에서 자주 쓰는 방식대로 1=true, 0=false다.
 */
int http_query_is_single_statement(const char *sql);

/*
 * JSON 요청 body를 ApiQueryRequest로 바꾼다.
 *
 * content_type은 정확히 "application/json"이어야 한다.
 * body는 {"sql":"...","request_id":"..."} 형태의 JSON object여야 한다.
 * request_id는 선택값이라 없어도 된다.
 *
 * out_request는 api_query_request_init() 이후 전달하는 것을 권장한다.
 * 성공 시 소유권은 호출자에게 넘어가며 api_query_request_free()로 정리한다.
 */
int http_parse_query_request(const char *content_type,
                             const char *body,
                             ApiQueryRequest *out_request);

#endif /* HTTP_REQUEST_H */
