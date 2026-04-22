#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "../include/http_request.h"

static void test_request_lifecycle(void) {
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(request.sql == NULL);
    assert(request.request_id == NULL);

    api_query_request_free(&request);
}

static void test_parse_sql_only(void) {
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT * FROM users;\"}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT * FROM users;") == 0);
    assert(request.request_id == NULL);

    api_query_request_free(&request);
}

static void test_parse_request_id_and_unknown_field(void) {
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json",
               "{\"unknown\":123,\"sql\":\"SELECT name FROM users\","
               "\"request_id\":\"req-7\"}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT name FROM users") == 0);
    assert(strcmp(request.request_id, "req-7") == 0);

    api_query_request_free(&request);
}

static void test_parse_escaped_strings(void) {
    ApiQueryRequest request;

    api_query_request_init(&request);
    assert(http_parse_query_request(
               "application/json",
               "{\"sql\":\"SELECT \\\"name\\\" FROM users\","
               "\"request_id\":\"req-\\u0037\"}",
               &request) == HTTP_REQUEST_OK);
    assert(strcmp(request.sql, "SELECT \"name\" FROM users") == 0);
    assert(strcmp(request.request_id, "req-7") == 0);

    api_query_request_free(&request);
}

static void test_parse_failures(void) {
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
               "{\"request_id\":\"req-1\"}",
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
               "{\"sql\":\"SELECT * FROM users\",\"request_id\":7}",
               &request) == HTTP_REQUEST_ERR_BAD_REQUEST);

    api_query_request_free(&request);
}

static void test_single_statement_policy(void) {
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
    test_request_lifecycle();
    test_parse_sql_only();
    test_parse_request_id_and_unknown_field();
    test_parse_escaped_strings();
    test_parse_failures();
    test_single_statement_policy();
    return 0;
}
