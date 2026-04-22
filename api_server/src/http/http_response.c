#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/http_response.h"

/*
 * 이 파일은 서버 내부 결과(EngineResponse)를 HTTP JSON 응답으로 바꾼다.
 * 문자열을 이어 붙일 때마다 malloc 크기를 직접 계산하면 실수하기 쉬우므로,
 * JsonBuffer라는 작은 동적 버퍼를 만들어서 안전하게 붙인다.
 *
 * 큰 흐름:
 *   1. JsonBuffer에 JSON 문자열을 차례대로 붙인다.
 *   2. 문자열 값은 반드시 json_append_escaped()로 감싼다.
 *   3. 완성된 JSON body를 HttpResponse.body로 넘긴다.
 *   4. 호출자는 나중에 http_response_free()로 body를 정리한다.
 */

typedef struct {
    /* 실제 JSON 문자열이 저장되는 메모리 주소다. */
    char *data;

    /* 현재까지 data에 채운 글자 수다. 마지막 '\0'은 제외한다. */
    size_t len;

    /* 현재 malloc/realloc으로 확보해 둔 전체 공간 크기다. */
    size_t cap;
} JsonBuffer;

static int json_buffer_init(JsonBuffer *buf) {
    /*
     * 처음부터 너무 작은 메모리를 잡으면 append할 때마다 realloc이 자주 일어난다.
     * 128바이트는 작은 JSON 응답을 담기 적당한 시작 크기다.
     */
    buf->cap = 128;
    buf->len = 0;
    buf->data = (char *)malloc(buf->cap);
    if (!buf->data) return 0;
    buf->data[0] = '\0';
    return 1;
}

static void json_buffer_free(JsonBuffer *buf) {
    /*
     * JsonBuffer가 아직 HttpResponse에게 넘겨지지 않은 상태라면 여기서 정리한다.
     * free 후에는 포인터와 길이를 초기화해서 실수로 다시 쓰는 일을 줄인다.
     */
    if (!buf) return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int json_buffer_reserve(JsonBuffer *buf, size_t extra) {
    size_t needed = buf->len + extra + 1;
    char *next;
    size_t next_cap;

    /*
     * 새 문자열을 붙일 공간이 부족하면 버퍼 크기를 2배씩 키운다.
     * len은 현재 사용량, cap은 현재 확보된 전체 공간이다.
     */
    if (needed <= buf->cap) return 1;

    next_cap = buf->cap;
    while (next_cap < needed) next_cap *= 2;

    next = (char *)realloc(buf->data, next_cap);
    if (!next) return 0;

    buf->data = next;
    buf->cap = next_cap;
    return 1;
}

static int json_append_raw(JsonBuffer *buf, const char *text) {
    size_t len = strlen(text);

    /*
     * raw append는 이미 JSON 문법 조각인 문자열을 그대로 붙인다.
     * 예: "{\"status\":\"ok\"}" 또는 ",\"data\":"
     *
     * 사용자가 보낸 값에는 raw append를 쓰면 안 된다.
     * 사용자 문자열은 quote/backslash 때문에 JSON이 깨질 수 있으므로
     * json_append_escaped()를 써야 한다.
     */
    if (!json_buffer_reserve(buf, len)) return 0;
    memcpy(buf->data + buf->len, text, len + 1);
    buf->len += len;
    return 1;
}

static int json_append_fmt(JsonBuffer *buf, const char *fmt, ...) {
    va_list ap;
    va_list ap_copy;
    int needed;

    /*
     * 숫자 같은 값은 printf 형식으로 붙이면 편하다.
     * 먼저 vsnprintf(NULL, 0, ...)로 필요한 길이를 계산한 뒤,
     * 공간을 확보하고 실제 문자열을 쓴다.
     */
    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap_copy);
        return 0;
    }

    if (!json_buffer_reserve(buf, (size_t)needed)) {
        va_end(ap_copy);
        return 0;
    }

    vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, ap_copy);
    va_end(ap_copy);
    buf->len += (size_t)needed;
    return 1;
}

static int json_append_escaped(JsonBuffer *buf, const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    /*
     * JSON 문자열 안에서는 "와 \ 같은 문자를 그대로 둘 수 없다.
     * 예를 들어 kim"lee는 "kim\"lee"처럼 escape해야 올바른 JSON이 된다.
     */
    if (!json_append_raw(buf, "\"")) return 0;

    while (*p) {
        unsigned char ch = *p++;
        char encoded[7];

        switch (ch) {
            case '"':
                if (!json_append_raw(buf, "\\\"")) return 0;
                break;
            case '\\':
                if (!json_append_raw(buf, "\\\\")) return 0;
                break;
            case '\n':
                if (!json_append_raw(buf, "\\n")) return 0;
                break;
            case '\r':
                if (!json_append_raw(buf, "\\r")) return 0;
                break;
            case '\t':
                if (!json_append_raw(buf, "\\t")) return 0;
                break;
            default:
                if (ch < 0x20) {
                    /*
                     * 눈에 보이지 않는 제어문자는 JSON에 그대로 넣을 수 없다.
                     * \u0001 같은 안전한 escape 형태로 바꾼다.
                     */
                    snprintf(encoded, sizeof(encoded), "\\u%04x", ch);
                    if (!json_append_raw(buf, encoded)) return 0;
                } else {
                    if (!json_buffer_reserve(buf, 1)) return 0;
                    buf->data[buf->len++] = (char)ch;
                    buf->data[buf->len] = '\0';
                }
                break;
        }
    }

    return json_append_raw(buf, "\"");
}

static void set_content_type(HttpResponse *response) {
    /*
     * B 모듈에서 만드는 body는 모두 JSON이다.
     * server 쪽은 이 content_type을 보고 HTTP header를 만들 수 있다.
     */
    strncpy(response->content_type,
            "application/json",
            sizeof(response->content_type) - 1);
    response->content_type[sizeof(response->content_type) - 1] = '\0';
}

static int finish_response(HttpResponse *response,
                           int status_code,
                           JsonBuffer *buf) {
    /*
     * JsonBuffer가 만든 body 메모리를 HttpResponse에게 넘긴다.
     * 넘긴 뒤에는 buf->data를 NULL로 바꿔서 중복 free가 일어나지 않게 한다.
     */
    response->status_code = status_code;
    set_content_type(response);
    response->body = buf->data;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return HTTP_RESPONSE_OK;
}

void http_response_init(HttpResponse *response) {
    /*
     * 빈 응답 상태로 만든다.
     * body가 NULL이면 아직 할당된 JSON 문자열이 없다는 뜻이다.
     */
    if (!response) return;

    response->status_code = 0;
    response->content_type[0] = '\0';
    response->body = NULL;
}

void http_response_free(HttpResponse *response) {
    /*
     * HttpResponse.body는 malloc된 문자열이다.
     * 응답을 다 썼거나 새 응답으로 덮어쓰기 전에 반드시 정리한다.
     */
    if (!response) return;

    free(response->body);
    response->body = NULL;
    response->status_code = 0;
    response->content_type[0] = '\0';
}

int http_build_health_response(HttpResponse *out_response) {
    JsonBuffer buf;

    if (!out_response) return HTTP_RESPONSE_ERR_INVALID_ARG;

    /*
     * response builder들은 재사용 가능한 HttpResponse를 받는다.
     * 이전 body가 남아 있으면 새 body를 넣기 전에 먼저 정리한다.
     */
    http_response_free(out_response);

    /*
     * health 응답은 서버가 살아 있는지 확인하는 가장 단순한 응답이다.
     * DB 실행 결과가 필요 없으므로 status만 보낸다.
     */
    if (!json_buffer_init(&buf)) return HTTP_RESPONSE_ERR_NO_MEMORY;
    if (!json_append_raw(&buf, "{\"status\":\"ok\"}")) {
        json_buffer_free(&buf);
        return HTTP_RESPONSE_ERR_NO_MEMORY;
    }

    return finish_response(out_response, 200, &buf);
}

int http_build_query_success_response(const EngineResponse *engine_response,
                                      const char *request_id,
                                      HttpResponse *out_response) {
    JsonBuffer buf;
    int i;
    int r;
    int c;

    if (!engine_response || !out_response) {
        return HTTP_RESPONSE_ERR_INVALID_ARG;
    }

    http_response_free(out_response);

    /*
     * 성공 응답은 SELECT와 INSERT만 처리한다.
     * ENGINE_RESULT_ERROR는 error response builder가 담당한다.
     */
    if (engine_response->type != ENGINE_RESULT_SELECT &&
        engine_response->type != ENGINE_RESULT_INSERT) {
        return HTTP_RESPONSE_ERR_INVALID_ARG;
    }

    if (!json_buffer_init(&buf)) return HTTP_RESPONSE_ERR_NO_MEMORY;

    /*
     * 모든 성공 응답은 status:"ok"로 시작한다.
     * request_id는 요청에 있었을 때만 넣는다.
     */
    if (!json_append_raw(&buf, "{\"status\":\"ok\"")) goto oom;
    if (request_id) {
        if (!json_append_raw(&buf, ",\"request_id\":")) goto oom;
        if (!json_append_escaped(&buf, request_id)) goto oom;
    }
    if (!json_append_raw(&buf, ",\"data\":")) goto oom;

    if (engine_response->type == ENGINE_RESULT_SELECT) {
        const EngineSelectResult *select = &engine_response->select;

        /*
         * SELECT 결과는 컬럼 목록과 행 목록을 나눠서 보낸다.
         * rows는 2차원 배열이다.
         * 예: [["1","kim"],["2","lee"]]
         */
        if (!json_append_raw(&buf, "{\"columns\":[")) goto oom;
        for (i = 0; i < select->column_count; i++) {
            /*
             * JSON 배열은 값 사이에 콤마가 필요하지만 첫 번째 값 앞에는 콤마가 없다.
             * 그래서 i > 0일 때만 콤마를 붙인다.
             */
            if (i > 0 && !json_append_raw(&buf, ",")) goto oom;
            if (!json_append_escaped(&buf, select->columns ? select->columns[i] : "")) {
                goto oom;
            }
        }

        if (!json_append_raw(&buf, "],\"rows\":[")) goto oom;
        for (r = 0; r < select->row_count; r++) {
            /*
             * rows는 행 배열의 배열이다.
             * row_count가 2이고 column_count가 2라면 대략 이런 모양이다.
             * [["1","kim"],["2","lee"]]
             */
            if (r > 0 && !json_append_raw(&buf, ",")) goto oom;
            if (!json_append_raw(&buf, "[")) goto oom;
            for (c = 0; c < select->column_count; c++) {
                const char *value = "";
                if (select->rows && select->rows[r]) value = select->rows[r][c];
                if (c > 0 && !json_append_raw(&buf, ",")) goto oom;
                if (!json_append_escaped(&buf, value)) goto oom;
            }
            if (!json_append_raw(&buf, "]")) goto oom;
        }

        if (!json_append_fmt(&buf,
                             "],\"row_count\":%d}}",
                             select->row_count)) {
            goto oom;
        }
    } else {
        const EngineInsertResult *insert = &engine_response->insert;

        /*
         * INSERT는 영향을 받은 행 수를 기본으로 보내고,
         * AUTO_INCREMENT id가 있을 때만 generated_id를 추가한다.
         */
        if (!json_append_fmt(&buf,
                             "{\"affected_rows\":%d",
                             insert->affected_rows)) {
            goto oom;
        }
        if (insert->has_generated_id) {
            if (!json_append_fmt(&buf,
                                 ",\"generated_id\":%d",
                                 insert->generated_id)) {
                goto oom;
            }
        }
        if (!json_append_raw(&buf, "}}")) goto oom;
    }

    return finish_response(out_response, 200, &buf);

oom:
    /*
     * JSON을 만들다가 메모리가 부족해지면 중간 버퍼를 정리하고 실패를 알린다.
     * 이 경우 out_response에는 성공 body를 넣지 않는다.
     */
    json_buffer_free(&buf);
    return HTTP_RESPONSE_ERR_NO_MEMORY;
}

int http_build_error_response(int status_code,
                              const char *request_id,
                              const char *error_code,
                              const char *message,
                              HttpResponse *out_response) {
    JsonBuffer buf;

    if (!error_code || !message || !out_response) {
        return HTTP_RESPONSE_ERR_INVALID_ARG;
    }

    /*
     * 실패 응답도 request_id가 있으면 그대로 돌려준다.
     * 클라이언트는 이 값으로 어떤 요청이 실패했는지 쉽게 찾을 수 있다.
     */
    http_response_free(out_response);

    if (!json_buffer_init(&buf)) return HTTP_RESPONSE_ERR_NO_MEMORY;

    if (!json_append_raw(&buf, "{\"status\":\"error\"")) goto oom;
    if (request_id) {
        if (!json_append_raw(&buf, ",\"request_id\":")) goto oom;
        if (!json_append_escaped(&buf, request_id)) goto oom;
    }
    if (!json_append_raw(&buf, ",\"error\":{\"code\":")) goto oom;
    if (!json_append_escaped(&buf, error_code)) goto oom;
    if (!json_append_raw(&buf, ",\"message\":")) goto oom;
    if (!json_append_escaped(&buf, message)) goto oom;
    if (!json_append_raw(&buf, "}}")) goto oom;

    return finish_response(out_response, status_code, &buf);

oom:
    json_buffer_free(&buf);
    return HTTP_RESPONSE_ERR_NO_MEMORY;
}

int http_status_from_engine_error(EngineErrorCode code) {
    /*
     * 엔진 내부 에러를 HTTP 의미로 바꾼다.
     * 문법/검증 문제는 클라이언트 요청 문제라 400,
     * 아직 지원하지 않는 SQL은 422/501,
     * 런타임 문제는 서버 문제라 500으로 둔다.
     */
    switch (code) {
        case ENGINE_ERR_PARSE:
        case ENGINE_ERR_VALIDATION:
            return 400;
        case ENGINE_ERR_UNSUPPORTED:
            return 422;
        case ENGINE_ERR_NOT_IMPLEMENTED:
            return 501;
        case ENGINE_ERR_RUNTIME:
        default:
            return 500;
    }
}

const char *http_error_code_from_engine_error(EngineErrorCode code) {
    /*
     * HTTP body에 노출할 공개 에러 코드다.
     * ENGINE_ERR_PARSE 같은 내부 enum 이름을 그대로 노출하지 않고,
     * 클라이언트가 읽기 쉬운 짧은 문자열로 바꾼다.
     */
    switch (code) {
        case ENGINE_ERR_PARSE:
            return "PARSE_ERROR";
        case ENGINE_ERR_VALIDATION:
            return "VALIDATION_ERROR";
        case ENGINE_ERR_UNSUPPORTED:
            return "UNSUPPORTED_SQL";
        case ENGINE_ERR_RUNTIME:
            return "ENGINE_RUNTIME_ERROR";
        case ENGINE_ERR_NOT_IMPLEMENTED:
        default:
            return "NOT_IMPLEMENTED";
    }
}
