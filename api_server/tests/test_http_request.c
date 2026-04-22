#include <assert.h>

#include "../include/http_request.h"

int main(void) {
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(request.sql == NULL);
    assert(request.request_id == NULL);

    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT * FROM users;\"}",
               &request) == HTTP_REQUEST_ERR_NOT_IMPLEMENTED);
    assert(http_query_is_single_statement("SELECT * FROM users;") == 0);

    api_query_request_free(&request);
    return 0;
}
