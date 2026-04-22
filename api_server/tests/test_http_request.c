#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "../include/http_request.h"

/*
 * 이 파일은 http_request.c의 단위 테스트다.
 * 단위 테스트는 "작은 함수 하나가 약속대로 동작하는지" 확인하는 코드다.
 * 서버를 실제로 띄우지 않고, request 파서 함수만 직접 호출해서 결과를 검사한다.
 */

static void test_request_lifecycle(void) {
    /*
     * init/free가 기본 메모리 상태를 안전하게 만드는지 확인한다.
     * request.sql이 NULL이면 아직 소유한 문자열이 없다는 뜻이다.
     */
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(request.sql == NULL);

    api_query_request_free(&request);
}

static void test_parse_sql_only(void) {
    /*
     * 가장 기본적인 성공 케이스다.
     * JSON body에 sql만 있을 때 request.sql에 문자열이 복사된다.
     */
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT * FROM users;\"}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT * FROM users;") == 0);

    api_query_request_free(&request);
}

static void test_parse_unknown_fields(void) {
    /*
     * unknown 필드는 API 확장성을 위해 무시해야 한다.
     * 예를 들어 클라이언트가 debug 값을 보내도 sql 파싱은 성공해야 한다.
     */
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json",
               "{\"unknown\":123,\"sql\":\"SELECT name FROM users\","
               "\"debug\":\"on\"}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT name FROM users") == 0);

    api_query_request_free(&request);
}

static void test_parse_escaped_strings(void) {
    /*
     * JSON 문자열에는 \" 또는 \u0037 같은 escape가 들어올 수 있다.
     * 파서는 이런 escape를 실제 문자로 바꿔서 저장해야 한다.
     */
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT \\\"name\\\" FROM users\"}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT \"name\" FROM users") == 0);

    api_query_request_free(&request);
}

static void test_parse_failures(void) {
    /*
     * 잘못된 요청을 거부하는지 확인한다.
     * 서버는 이상한 요청을 그대로 엔진에 넘기지 말고 HTTP request 단계에서 막아야 한다.
     */
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json; charset=utf-8",
               "{\"sql\":\"SELECT * FROM users\"}",
               &request) == HTTP_REQUEST_ERR_UNSUPPORTED_MEDIA_TYPE);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT * FROM users\"",
               &request) == HTTP_REQUEST_ERR_BAD_REQUEST);
    assert(http_parse_query_request(
               "application/json",
               "{\"debug\":\"missing sql\"}",
               &request) == HTTP_REQUEST_ERR_BAD_REQUEST);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":123}",
               &request) == HTTP_REQUEST_ERR_BAD_REQUEST);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"\"}",
               &request) == HTTP_REQUEST_ERR_BAD_REQUEST);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT * FROM users\",\"debug\":7}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT * FROM users") == 0);

    api_query_request_free(&request);
}

static void test_single_statement_policy(void) {
    /*
     * SQL 한 문장 규칙을 직접 검사한다.
     * 세미콜론이 없거나 끝에 하나 있는 건 허용하지만,
     * 세미콜론 뒤에 다른 SQL이 오면 거부해야 한다.
     */
    assert(http_query_is_single_statement("SELECT * FROM users") == 1);
    assert(http_query_is_single_statement("SELECT * FROM users;") == 1);
    assert(http_query_is_single_statement("SELECT * FROM users;   \n\t") == 1);
    assert(http_query_is_single_statement("SELECT ';' FROM users;") == 1);
    assert(http_query_is_single_statement("") == 0);
    assert(http_query_is_single_statement("   ;") == 0);
    assert(http_query_is_single_statement("SELECT * FROM users; SELECT * FROM users") == 0);
    assert(http_query_is_single_statement("SELECT * FROM users;;") == 0);
    assert(http_query_is_single_statement("SELECT 'unterminated FROM users;") == 0);
}

int main(void) {
    /*
     * 각 테스트 함수는 독립적인 작은 시나리오다.
     * assert가 실패하면 프로그램이 바로 중단되므로 make test가 실패한다.
     */
    test_request_lifecycle();
    test_parse_sql_only();
    test_parse_unknown_fields();
    test_parse_escaped_strings();
    test_parse_failures();
    test_single_statement_policy();
    return 0;
}
