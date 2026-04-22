#include <stdlib.h>

#include "../../include/http_response.h"

void http_response_init(HttpResponse *response) {
    if (!response) return;

    response->status_code = 0;
    response->content_type[0] = '\0';
    response->body = NULL;
}

void http_response_free(HttpResponse *response) {
    if (!response) return;

    free(response->body);
    response->body = NULL;
    response->status_code = 0;
    response->content_type[0] = '\0';
}

int http_build_health_response(HttpResponse *out_response) {
    if (!out_response) return HTTP_RESPONSE_ERR_INVALID_ARG;

    http_response_free(out_response);

    /* TODO: Serialize the GET /health JSON response. */
    return HTTP_RESPONSE_ERR_NOT_IMPLEMENTED;
}

int http_build_query_success_response(const EngineResponse *engine_response,
                                      const char *request_id,
                                      HttpResponse *out_response) {
    if (!engine_response || !out_response) {
        return HTTP_RESPONSE_ERR_INVALID_ARG;
    }

    (void)request_id;
    http_response_free(out_response);

    /* TODO: Serialize SELECT and INSERT success payloads. */
    return HTTP_RESPONSE_ERR_NOT_IMPLEMENTED;
}

int http_build_error_response(int status_code,
                              const char *request_id,
                              const char *error_code,
                              const char *message,
                              HttpResponse *out_response) {
    if (!error_code || !message || !out_response) {
        return HTTP_RESPONSE_ERR_INVALID_ARG;
    }

    (void)status_code;
    (void)request_id;
    http_response_free(out_response);

    /* TODO: Serialize error responses with status_code/error_code/message. */
    return HTTP_RESPONSE_ERR_NOT_IMPLEMENTED;
}

int http_status_from_engine_error(EngineErrorCode code) {
    (void)code;

    /* TODO: Define the engine-to-HTTP status mapping. */
    return 500;
}

const char *http_error_code_from_engine_error(EngineErrorCode code) {
    (void)code;

    /* TODO: Define the public error_code string mapping. */
    return "NOT_IMPLEMENTED";
}
