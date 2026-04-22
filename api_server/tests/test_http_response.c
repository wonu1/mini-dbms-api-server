#include <assert.h>
#include <string.h>

#include "../include/http_response.h"

int main(void) {
    EngineResponse payload = {0};
    HttpResponse response;

    http_response_init(&response);
    assert(response.body == NULL);

    assert(http_build_health_response(&response) ==
           HTTP_RESPONSE_ERR_NOT_IMPLEMENTED);

    payload.type = ENGINE_RESULT_SELECT;
    assert(http_build_query_success_response(&payload, "req-1", &response) ==
           HTTP_RESPONSE_ERR_NOT_IMPLEMENTED);

    assert(http_build_error_response(503, "req-2", "QUEUE_FULL", "queue full", &response) ==
           HTTP_RESPONSE_ERR_NOT_IMPLEMENTED);

    assert(http_status_from_engine_error(ENGINE_ERR_VALIDATION) == 500);
    assert(strcmp(http_error_code_from_engine_error(ENGINE_ERR_RUNTIME),
                  "NOT_IMPLEMENTED") == 0);

    http_response_free(&response);
    return 0;
}
