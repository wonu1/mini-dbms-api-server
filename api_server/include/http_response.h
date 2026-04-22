#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "engine_types.h"

/*
 * HTTP 응답 하나를 표현하는 구조체다.
 * 실제 socket 전송은 server 쪽 책임이고, B 모듈은 status/content-type/body를 만든다.
 */
typedef struct {
    /* HTTP 상태 코드다. 예: 200, 400, 500 */
    int status_code;

    /* 응답 body 형식이다. 여기서는 항상 "application/json"을 넣는다. */
    char content_type[32];

    /*
     * 클라이언트에게 보낼 JSON 문자열이다.
     * malloc된 메모리이므로 http_response_free()로 정리한다.
     */
    char *body;
} HttpResponse;

/*
 * http_response 모듈의 반환 코드다.
 * JSON 문자열 생성이 성공했는지, 실패했다면 어떤 종류인지 알려준다.
 */
typedef enum {
    /* 응답 생성 성공 */
    HTTP_RESPONSE_OK = 0,

    /* NULL 포인터처럼 함수 인자가 잘못됐다. */
    HTTP_RESPONSE_ERR_INVALID_ARG = -1,

    /* malloc/realloc 실패로 body를 만들 수 없다. */
    HTTP_RESPONSE_ERR_NO_MEMORY = -2,

    /* 예전 스텁 단계의 값이다. 현재 구현에서는 정상 흐름에서 쓰지 않는다. */
    HTTP_RESPONSE_ERR_NOT_IMPLEMENTED = -3
} HttpResponseStatus;

/*
 * out_response는 http_response_init() 후 사용하고 http_response_free()로 정리한다.
 * init은 빈 응답으로 만들고, free는 body 메모리를 해제한 뒤 다시 빈 상태로 만든다.
 */
void http_response_init(HttpResponse *response);
void http_response_free(HttpResponse *response);

/*
 * /health 응답을 만든다.
 * 결과 body: {"status":"ok"}
 */
int http_build_health_response(HttpResponse *out_response);

/*
 * 엔진 실행 성공 결과를 JSON 응답으로 만든다.
 * SELECT면 columns/rows/row_count를 담고, INSERT면 affected_rows를 담는다.
 * request_id가 NULL이 아니면 응답 최상위에 그대로 echo한다.
 */
int http_build_query_success_response(const EngineResponse *engine_response,
                                      const char *request_id,
                                      HttpResponse *out_response);

/*
 * 실패 응답을 JSON으로 만든다.
 * status_code는 실제 HTTP status가 되고, error_code/message는 body의 error 객체에 들어간다.
 */
int http_build_error_response(int status_code,
                              const char *request_id,
                              const char *error_code,
                              const char *message,
                              HttpResponse *out_response);

/*
 * 엔진 내부 에러 enum을 HTTP status code로 바꾼다.
 * 예: ENGINE_ERR_PARSE -> 400
 */
int http_status_from_engine_error(EngineErrorCode code);

/*
 * 엔진 내부 에러 enum을 클라이언트에게 보여줄 짧은 문자열 코드로 바꾼다.
 * 예: ENGINE_ERR_PARSE -> "PARSE_ERROR"
 */
const char *http_error_code_from_engine_error(EngineErrorCode code);

#endif /* HTTP_RESPONSE_H */
