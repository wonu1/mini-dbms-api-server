#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "engine_types.h"

typedef struct {
    int status_code;
    char content_type[32];
    char *body;
} HttpResponse;

typedef enum {
    HTTP_RESPONSE_OK = 0,
    HTTP_RESPONSE_ERR_INVALID_ARG = -1,
    HTTP_RESPONSE_ERR_NO_MEMORY = -2,
    HTTP_RESPONSE_ERR_NOT_IMPLEMENTED = -3
} HttpResponseStatus;

/* out_response는 http_response_init() 후 사용하고 http_response_free()로 정리한다. */
void http_response_init(HttpResponse *response);
void http_response_free(HttpResponse *response);

int http_build_health_response(HttpResponse *out_response);
int http_build_query_success_response(const EngineResponse *engine_response,
                                      const char *request_id,
                                      HttpResponse *out_response);
int http_build_error_response(int status_code,
                              const char *request_id,
                              const char *error_code,
                              const char *message,
                              HttpResponse *out_response);

int http_status_from_engine_error(EngineErrorCode code);
const char *http_error_code_from_engine_error(EngineErrorCode code);

#endif /* HTTP_RESPONSE_H */
