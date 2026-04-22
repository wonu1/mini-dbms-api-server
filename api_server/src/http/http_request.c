#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/http_request.h"

/*
 * 이 파일은 HTTP 요청의 JSON body를 우리 서버 내부 구조체로 바꾸는 일을 맡는다.
 * 외부 JSON 라이브러리를 쓰지 않기로 했기 때문에, 여기서는 우리가 필요한 만큼만
 * 직접 파싱한다. 현재 API가 필요한 값은 "sql"과 선택값 "request_id" 두 개다.
 */

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_json_string(const char **p, char **out) {
    const char *s;
    char *buf;
    size_t cap;
    size_t len = 0;

    /*
     * JSON 문자열은 반드시 큰따옴표로 시작한다.
     * 예: "SELECT * FROM users"
     */
    if (**p != '"') return 0;
    (*p)++;
    s = *p;
    cap = strlen(s) + 1;
    buf = (char *)malloc(cap);
    if (!buf) return -1;

    while (**p) {
        unsigned char ch = (unsigned char)**p;

        if (ch == '"') {
            (*p)++;
            buf[len] = '\0';
            *out = buf;
            return 1;
        }

        if (ch < 0x20) {
            free(buf);
            return 0;
        }

        if (ch == '\\') {
            int hv1;
            int hv2;

            (*p)++;
            switch (**p) {
                /*
                 * JSON 문자열 안에서 \" 또는 \n 같은 escape 문자를 실제 문자로
                 * 되돌린다. 그래야 request.sql에는 사용자가 의도한 문자열만 남는다.
                 */
                case '"': buf[len++] = '"'; (*p)++; break;
                case '\\': buf[len++] = '\\'; (*p)++; break;
                case '/': buf[len++] = '/'; (*p)++; break;
                case 'b': buf[len++] = '\b'; (*p)++; break;
                case 'f': buf[len++] = '\f'; (*p)++; break;
                case 'n': buf[len++] = '\n'; (*p)++; break;
                case 'r': buf[len++] = '\r'; (*p)++; break;
                case 't': buf[len++] = '\t'; (*p)++; break;
                case 'u':
                    /*
                     * \u0037 같은 유니코드 escape도 최소한 처리한다.
                     * 우리 SQL/request_id는 보통 ASCII라서 ASCII 범위만 실제 문자로
                     * 바꾸고, 그 밖의 문자는 '?'로 둔다.
                     */
                    hv1 = hex_value((*p)[1]);
                    hv2 = hex_value((*p)[2]);
                    if (hv1 < 0 || hv2 < 0 ||
                        hex_value((*p)[3]) < 0 || hex_value((*p)[4]) < 0) {
                        free(buf);
                        return 0;
                    }
                    if (hv1 == 0 && hv2 == 0) {
                        int low = hex_value((*p)[3]) * 16 + hex_value((*p)[4]);
                        buf[len++] = (low > 0 && low < 0x80) ? (char)low : '?';
                    } else {
                        buf[len++] = '?';
                    }
                    *p += 5;
                    break;
                default:
                    free(buf);
                    return 0;
            }
            continue;
        }

        buf[len++] = (char)ch;
        (*p)++;
    }

    free(buf);
    return 0;
}

static int skip_json_string(const char **p) {
    char *tmp = NULL;
    int status = parse_json_string(p, &tmp);
    free(tmp);
    return status;
}

static int skip_json_literal(const char **p) {
    const char *start = *p;

    while (**p && !isspace((unsigned char)**p) &&
           **p != ',' && **p != '}' && **p != ']') {
        (*p)++;
    }

    return *p != start;
}

static int skip_json_value(const char **p) {
    int depth = 0;

    /*
     * unknown field는 무시해야 하므로 값 하나를 통째로 건너뛴다.
     * 문자열, 숫자/true/false/null, 간단한 object/array를 모두 지나갈 수 있게 한다.
     */
    skip_ws(p);
    if (**p == '"') return skip_json_string(p);
    if (**p != '{' && **p != '[') return skip_json_literal(p);

    do {
        if (**p == '"') {
            int status = skip_json_string(p);
            if (status <= 0) return status;
            continue;
        }
        if (**p == '{' || **p == '[') depth++;
        if (**p == '}' || **p == ']') depth--;
        (*p)++;
    } while (**p && depth > 0);

    return depth == 0;
}

void api_query_request_init(ApiQueryRequest *request) {
    if (!request) return;

    request->sql = NULL;
    request->request_id = NULL;
}

void api_query_request_free(ApiQueryRequest *request) {
    if (!request) return;

    free(request->sql);
    free(request->request_id);
    api_query_request_init(request);
}

void query_job_init(QueryJob *job) {
    if (!job) return;

    job->client_fd = -1;
    api_query_request_init(&job->request);
}

void query_job_free(QueryJob *job) {
    if (!job) return;

    api_query_request_free(&job->request);
    job->client_fd = -1;
}

int http_query_is_single_statement(const char *sql) {
    int seen_semicolon = 0;
    int in_string = 0;
    int has_content = 0;
    const char *p;

    /*
     * API 서버는 한 요청에 SQL 한 문장만 받는다.
     * 허용:
     *   SELECT * FROM users
     *   SELECT * FROM users;
     * 거부:
     *   SELECT * FROM users; SELECT * FROM users
     */
    if (!sql) return 0;

    for (p = sql; *p; p++) {
        if (*p == '\'') {
            /*
             * SQL 문자열 안의 세미콜론은 문장 끝이 아니다.
             * 예: SELECT ';' FROM users;
             */
            in_string = !in_string;
            continue;
        }

        if (!in_string && *p == ';') {
            seen_semicolon = 1;
            p++;
            break;
        }

        if (!isspace((unsigned char)*p)) has_content = 1;
    }

    if (in_string) return 0;
    if (!has_content) return 0;
    if (!seen_semicolon) return 1;

    while (*p) {
        if (!isspace((unsigned char)*p)) return 0;
        p++;
    }

    return 1;
}

int http_parse_query_request(const char *content_type,
                             const char *body,
                             ApiQueryRequest *out_request) {
    const char *p = body;
    char *sql = NULL;
    char *request_id = NULL;
    int status = HTTP_REQUEST_ERR_BAD_REQUEST;

    if (!content_type || !body || !out_request) {
        return HTTP_REQUEST_ERR_INVALID_ARG;
    }

    /*
     * out_request가 이전 요청 데이터를 들고 있을 수 있으므로 먼저 비운다.
     * 성공하면 새 sql/request_id 소유권을 out_request가 가진다.
     */
    api_query_request_free(out_request);

    if (strcmp(content_type, "application/json") != 0) {
        return HTTP_REQUEST_ERR_UNSUPPORTED_MEDIA_TYPE;
    }

    /*
     * 최소 JSON object 파싱 흐름:
     * 1. body가 { 로 시작하는지 확인한다.
     * 2. "key": value 쌍을 하나씩 읽는다.
     * 3. key가 sql/request_id면 문자열 값으로 저장한다.
     * 4. 모르는 key면 값만 건너뛰고 계속 진행한다.
     */
    skip_ws(&p);
    if (*p != '{') goto fail;
    p++;
    skip_ws(&p);

    while (*p && *p != '}') {
        char *key = NULL;
        char *value = NULL;
        int parsed;

        parsed = parse_json_string(&p, &key);
        if (parsed <= 0) {
            status = parsed < 0 ? HTTP_REQUEST_ERR_INVALID_ARG
                                : HTTP_REQUEST_ERR_BAD_REQUEST;
            goto fail;
        }

        skip_ws(&p);
        if (*p != ':') {
            free(key);
            goto fail;
        }
        p++;
        skip_ws(&p);

        if (strcmp(key, "sql") == 0 || strcmp(key, "request_id") == 0) {
            parsed = parse_json_string(&p, &value);
            if (parsed <= 0) {
                free(key);
                status = parsed < 0 ? HTTP_REQUEST_ERR_INVALID_ARG
                                    : HTTP_REQUEST_ERR_BAD_REQUEST;
                goto fail;
            }

            if (strcmp(key, "sql") == 0) {
                free(sql);
                sql = value;
            } else {
                free(request_id);
                request_id = value;
            }
        } else {
            parsed = skip_json_value(&p);
            if (parsed <= 0) {
                free(key);
                status = parsed < 0 ? HTTP_REQUEST_ERR_INVALID_ARG
                                    : HTTP_REQUEST_ERR_BAD_REQUEST;
                goto fail;
            }
        }

        free(key);
        skip_ws(&p);
        if (*p == ',') {
            p++;
            skip_ws(&p);
            if (*p == '}') goto fail;
            continue;
        }
        if (*p != '}') goto fail;
    }

    if (*p != '}') goto fail;
    p++;
    skip_ws(&p);
    if (*p != '\0') goto fail;
    if (!sql || sql[0] == '\0') goto fail;
    if (!http_query_is_single_statement(sql)) goto fail;

    out_request->sql = sql;
    out_request->request_id = request_id;
    return HTTP_REQUEST_OK;

fail:
    free(sql);
    free(request_id);
    return status;
}
