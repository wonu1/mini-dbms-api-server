#include <assert.h>
#include <string.h>

#include "../include/http_response.h"

/*
 * 이 파일은 http_response.c의 단위 테스트다.
 * 엔진을 실제로 실행하지 않고, 가짜 EngineResponse를 만들어서
 * JSON 응답이 약속한 모양으로 나오는지만 확인한다.
 */

static void assert_json_response(const HttpResponse *response,
                                 int status_code,
                                 const char *body) {
    /*
     * 여러 테스트에서 반복되는 검사를 한 함수로 묶었다.
     * status_code, content_type, body가 모두 기대값과 같아야 통과한다.
     */
    assert(response->status_code == status_code);
    assert(strcmp(response->content_type, "application/json") == 0);
    assert(response->body != NULL);
    assert(strcmp(response->body, body) == 0);
}

static void test_health_response(void) {
    /*
     * /health는 서버가 살아 있는지 확인하는 가장 단순한 응답이다.
     * body는 {"status":"ok"} 하나면 충분하다.
     */
    HttpResponse response;

    http_response_init(&response);
    assert(response.body == NULL);

    assert(http_build_health_response(&response) == HTTP_RESPONSE_OK);
    assert_json_response(&response, 200, "{\"status\":\"ok\"}");

    http_response_free(&response);
}

static void test_select_success_response(void) {
    /*
     * SELECT 성공 응답을 검사한다.
     * EngineResponse 안의 columns/rows가 JSON 배열로 잘 바뀌어야 한다.
     */
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
    /*
     * INSERT 성공 응답 중 generated_id가 있는 경우다.
     * AUTO_INCREMENT로 새 id가 생긴 상황을 흉내 낸다.
     */
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
    /*
     * INSERT는 성공했지만 generated_id가 없는 경우다.
     * 이때는 JSON에 generated_id 필드 자체가 없어야 한다.
     */
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
    /*
     * 에러 응답은 status:"error"와 error 객체를 가진다.
     * request_id가 있으면 실패 응답에도 그대로 돌려준다.
     */
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
    /*
     * 엔진 내부 에러를 HTTP status code와 public error code로 바꾸는 규칙을 검사한다.
     * 이 매핑이 있어야 클라이언트가 실패 원인을 구분할 수 있다.
     */
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
    /*
     * JSON 문자열 escaping 테스트다.
     * 따옴표, 역슬래시, 줄바꿈, 탭이 들어가도 깨지지 않는 JSON을 만들어야 한다.
     */
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
    /*
     * assert 기반 테스트는 성공하면 조용히 끝난다.
     * 실패하면 어느 assert에서 멈췄는지 출력되고 make test가 실패한다.
     */
    test_health_response();
    test_select_success_response();
    test_insert_success_response();
    test_insert_without_generated_id();
    test_error_response();
    test_error_mapping();
    test_json_escaping();
    return 0;
}
