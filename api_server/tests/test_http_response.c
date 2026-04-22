#include <assert.h>
#include <string.h>

#include "../include/http_response.h"

static void assert_json_response(const HttpResponse *response,
                                 int status_code,
                                 const char *body) {
    assert(response->status_code == status_code);
    assert(strcmp(response->content_type, "application/json") == 0);
    assert(response->body != NULL);
    assert(strcmp(response->body, body) == 0);
}

static void test_health_response(void) {
    HttpResponse response;

    http_response_init(&response);
    assert(response.body == NULL);

    assert(http_build_health_response(&response) == HTTP_RESPONSE_OK);
    assert_json_response(&response, 200, "{\"status\":\"ok\"}");

    http_response_free(&response);
}

static void test_select_success_response(void) {
    char *columns[] = {"id", "name"};
    char *row0[] = {"1", "kim"};
    char *row1[] = {"2", "lee"};
    char **rows[] = {row0, row1};
    EngineResponse payload = {0};
    HttpResponse response;

    payload.type = ENGINE_RESULT_SELECT;
    payload.select.columns = columns;
    payload.select.column_count = 2;
    payload.select.rows = rows;
    payload.select.row_count = 2;

    http_response_init(&response);
    assert(http_build_query_success_response(&payload, "req-1", &response) ==
           HTTP_RESPONSE_OK);
    assert_json_response(
        &response,
        200,
        "{\"status\":\"ok\",\"request_id\":\"req-1\","
        "\"data\":{\"columns\":[\"id\",\"name\"],"
        "\"rows\":[[\"1\",\"kim\"],[\"2\",\"lee\"]],\"row_count\":2}}");

    http_response_free(&response);
}

static void test_insert_success_response(void) {
    EngineResponse payload = {0};
    HttpResponse response;

    payload.type = ENGINE_RESULT_INSERT;
    payload.insert.affected_rows = 1;
    payload.insert.has_generated_id = 1;
    payload.insert.generated_id = 42;

    http_response_init(&response);
    assert(http_build_query_success_response(&payload, NULL, &response) ==
           HTTP_RESPONSE_OK);
    assert_json_response(
        &response,
        200,
        "{\"status\":\"ok\",\"data\":{\"affected_rows\":1,\"generated_id\":42}}");

    http_response_free(&response);
}

static void test_insert_without_generated_id(void) {
    EngineResponse payload = {0};
    HttpResponse response;

    payload.type = ENGINE_RESULT_INSERT;
    payload.insert.affected_rows = 1;

    http_response_init(&response);
    assert(http_build_query_success_response(&payload, "req-2", &response) ==
           HTTP_RESPONSE_OK);
    assert_json_response(
        &response,
        200,
        "{\"status\":\"ok\",\"request_id\":\"req-2\","
        "\"data\":{\"affected_rows\":1}}");

    http_response_free(&response);
}

static void test_error_response(void) {
    HttpResponse response;

    http_response_init(&response);
    assert(http_build_error_response(
               503,
               "req-3",
               "QUEUE_FULL",
               "queue full",
               &response) == HTTP_RESPONSE_OK);
    assert_json_response(
        &response,
        503,
        "{\"status\":\"error\",\"request_id\":\"req-3\","
        "\"error\":{\"code\":\"QUEUE_FULL\",\"message\":\"queue full\"}}");

    http_response_free(&response);
}

static void test_error_mapping(void) {
    assert(http_status_from_engine_error(ENGINE_ERR_PARSE) == 400);
    assert(http_status_from_engine_error(ENGINE_ERR_VALIDATION) == 400);
    assert(http_status_from_engine_error(ENGINE_ERR_UNSUPPORTED) == 422);
    assert(http_status_from_engine_error(ENGINE_ERR_RUNTIME) == 500);
    assert(http_status_from_engine_error(ENGINE_ERR_NOT_IMPLEMENTED) == 501);

    assert(strcmp(http_error_code_from_engine_error(ENGINE_ERR_PARSE),
                  "PARSE_ERROR") == 0);
    assert(strcmp(http_error_code_from_engine_error(ENGINE_ERR_VALIDATION),
                  "VALIDATION_ERROR") == 0);
    assert(strcmp(http_error_code_from_engine_error(ENGINE_ERR_UNSUPPORTED),
                  "UNSUPPORTED_SQL") == 0);
    assert(strcmp(http_error_code_from_engine_error(ENGINE_ERR_RUNTIME),
                  "ENGINE_RUNTIME_ERROR") == 0);
    assert(strcmp(http_error_code_from_engine_error(ENGINE_ERR_NOT_IMPLEMENTED),
                  "NOT_IMPLEMENTED") == 0);
}

static void test_json_escaping(void) {
    char *columns[] = {"na\"me", "line"};
    char *row0[] = {"a\\b", "x\ny\tz"};
    char **rows[] = {row0};
    EngineResponse payload = {0};
    HttpResponse response;

    payload.type = ENGINE_RESULT_SELECT;
    payload.select.columns = columns;
    payload.select.column_count = 2;
    payload.select.rows = rows;
    payload.select.row_count = 1;

    http_response_init(&response);
    assert(http_build_query_success_response(&payload, "r\"\\\n", &response) ==
           HTTP_RESPONSE_OK);
    assert_json_response(
        &response,
        200,
        "{\"status\":\"ok\",\"request_id\":\"r\\\"\\\\\\n\","
        "\"data\":{\"columns\":[\"na\\\"me\",\"line\"],"
        "\"rows\":[[\"a\\\\b\",\"x\\ny\\tz\"]],\"row_count\":1}}");

    http_response_free(&response);
}

int main(void) {
    test_health_response();
    test_select_success_response();
    test_insert_success_response();
    test_insert_without_generated_id();
    test_error_response();
    test_error_mapping();
    test_json_escaping();
    return 0;
}
