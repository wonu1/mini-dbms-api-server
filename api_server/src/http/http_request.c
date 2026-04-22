#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/http_request.h"

/*
 * 이 파일은 HTTP 요청의 JSON body를 우리 서버 내부 구조체로 바꾸는 일을 맡는다.
 * 외부 JSON 라이브러리를 쓰지 않기로 했기 때문에, 여기서는 우리가 필요한 만큼만
 * 직접 파싱한다. 현재 API가 필요한 값은 "sql"과 선택값 "request_id" 두 개다.
 *
 * 큰 흐름은 아래와 같다.
 *   1. Content-Type이 application/json인지 확인한다.
 *   2. JSON object에서 key/value를 하나씩 읽는다.
 *   3. key가 "sql"이면 SQL 문자열을 저장한다.
 *   4. key가 "request_id"이면 추적용 문자열을 저장한다.
 *   5. 모르는 key는 버리지 않고 "값 하나를 읽고 건너뛴다".
 *   6. SQL이 한 문장인지 검사한다.
 *
 * 이 파일의 함수들은 malloc으로 만든 문자열을 ApiQueryRequest 안에 넣는다.
 * 따라서 성공한 요청은 나중에 api_query_request_free()로 정리해야 한다.
 */

static void skip_ws(const char **p) {
    /*
     * JSON에서는 key 앞뒤, 콜론(:) 앞뒤, 콤마(,) 뒤에 공백이 올 수 있다.
     * 예: { "sql" : "SELECT * FROM users" }
     *
     * p는 문자열을 가리키는 포인터다. 여기서는 공백이 아닌 글자를 만날 때까지
     * p를 앞으로 이동시킨다. const char **를 받는 이유는 호출한 쪽의 포인터도
     * 함께 움직여야 하기 때문이다.
     */
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int hex_value(char c) {
    /*
     * JSON unicode escape는 \u0037처럼 16진수 숫자를 쓴다.
     * 이 함수는 문자 하나를 16진수 값으로 바꾼다.
     * 예: 'A' -> 10, 'f' -> 15, '7' -> 7
     * 16진수가 아니면 -1을 돌려줘서 잘못된 escape임을 알린다.
     */
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
     *
     * 반환값 규칙:
     *   1  = 성공, *out에 malloc된 문자열이 들어간다.
     *   0  = JSON 문법이 잘못됐다.
     *  -1  = 메모리 할당에 실패했다.
     */
    if (**p != '"') return 0;
    (*p)++;
    s = *p;
    /*
     * 최악의 경우 escape가 하나도 없으면 결과 문자열 길이는 남은 입력보다 길 수 없다.
     * 그래서 strlen(s) + 1만큼 잡으면 충분하다.
     */
    cap = strlen(s) + 1;
    buf = (char *)malloc(cap);
    if (!buf) return -1;

    while (**p) {
        unsigned char ch = (unsigned char)**p;

        if (ch == '"') {
            /*
             * 닫는 큰따옴표를 만나면 문자열 하나가 끝난 것이다.
             * p는 닫는 따옴표 다음 위치로 옮기고, 완성된 buf를 호출자에게 넘긴다.
             */
            (*p)++;
            buf[len] = '\0';
            *out = buf;
            return 1;
        }

        if (ch < 0x20) {
            /*
             * JSON 문자열 안에는 raw newline 같은 제어문자가 그대로 들어올 수 없다.
             * 줄바꿈은 실제 줄바꿈 문자가 아니라 \n으로 escape되어 있어야 한다.
             */
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
                     *
                     * 예: \u0037 -> '7'
                     * 예: \uAC00 -> '?'  (한글 같은 비 ASCII는 여기서 완전 변환하지 않음)
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

    /*
     * while을 빠져나왔다는 것은 문자열이 끝나기 전에 입력이 끝났다는 뜻이다.
     * 예: {"sql":"SELECT
     */
    free(buf);
    return 0;
}

static int skip_json_string(const char **p) {
    /*
     * unknown field의 값이 문자열일 때 사용한다.
     * parse_json_string()으로 정확히 읽되, 우리는 그 값을 저장할 필요가 없으니
     * 바로 free한다.
     */
    char *tmp = NULL;
    int status = parse_json_string(p, &tmp);
    free(tmp);
    return status;
}

static int skip_json_literal(const char **p) {
    const char *start = *p;

    /*
     * 숫자, true, false, null 같은 값은 여기서 건너뛴다.
     * 콤마, 닫는 중괄호, 닫는 대괄호, 공백을 만나면 값 하나가 끝났다고 본다.
     * 예:
     *   123
     *   true
     *   null
     */
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

    /*
     * unknown field의 값이 object나 array일 수도 있다.
     * 예: {"debug":{"trace":true},"sql":"SELECT ..."}
     *
     * depth는 괄호가 얼마나 열려 있는지 세는 값이다.
     * { 또는 [ 를 만나면 +1, } 또는 ] 를 만나면 -1 한다.
     * depth가 다시 0이 되면 object/array 하나를 다 건너뛴 것이다.
     */
    do {
        if (**p == '"') {
            /*
             * object/array 안의 문자열에는 {, }, [, ] 같은 문자가 들어 있을 수 있다.
             * 문자열 안의 괄호는 구조가 아니므로 통째로 건너뛰어야 한다.
             */
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
    /*
     * ApiQueryRequest는 "파싱된 요청"을 담는 작은 구조체다.
     * 처음에는 아무 문자열도 소유하지 않으므로 NULL로 시작한다.
     */
    if (!request) return;

    request->sql = NULL;
    request->request_id = NULL;
}

void api_query_request_free(ApiQueryRequest *request) {
    /*
     * http_parse_query_request()가 성공하면 request->sql/request_id는 malloc된 문자열이다.
     * 따라서 요청 처리가 끝나면 반드시 free해야 한다.
     *
     * free(NULL)은 안전하므로 request_id가 없던 요청도 같은 함수로 정리할 수 있다.
     */
    if (!request) return;

    free(request->sql);
    free(request->request_id);
    api_query_request_init(request);
}

void query_job_init(QueryJob *job) {
    /*
     * QueryJob은 worker queue에 들어가는 작업 단위다.
     * 아직 연결된 client socket이 없다는 뜻으로 client_fd를 -1로 둔다.
     */
    if (!job) return;

    job->client_fd = -1;
    api_query_request_init(&job->request);
}

void query_job_free(QueryJob *job) {
    /*
     * QueryJob 안에는 ApiQueryRequest가 들어 있으므로,
     * job을 정리할 때 내부 request 문자열도 같이 정리한다.
     */
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
            /*
             * 문자열 밖에서 세미콜론을 만나면 SQL 한 문장이 끝났다고 본다.
             * 이제 뒤에 다른 문장이 붙어 있는지 확인하기 위해 loop를 멈춘다.
             */
            seen_semicolon = 1;
            p++;
            break;
        }

        /*
         * 공백만 있는 SQL은 유효한 한 문장이 아니다.
         * 그래서 실제 글자 하나라도 있었는지 기록한다.
         */
        if (!isspace((unsigned char)*p)) has_content = 1;
    }

    /*
     * 작은따옴표가 닫히지 않았다면 SQL 문자열이 깨져 있다고 본다.
     * 예: SELECT 'abc FROM users;
     */
    if (in_string) return 0;
    if (!has_content) return 0;
    if (!seen_semicolon) return 1;

    /*
     * 세미콜론 뒤에는 공백만 허용한다.
     * 뒤에 글자가 나오면 두 번째 SQL 문장이 있거나 이상한 입력이므로 거부한다.
     */
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

        /*
         * JSON object의 key는 항상 문자열이어야 한다.
         * 예: "sql"
         */
        parsed = parse_json_string(&p, &key);
        if (parsed <= 0) {
            status = parsed < 0 ? HTTP_REQUEST_ERR_INVALID_ARG
                                : HTTP_REQUEST_ERR_BAD_REQUEST;
            goto fail;
        }

        skip_ws(&p);
        if (*p != ':') {
            /*
             * key 다음에는 반드시 콜론(:)이 와야 한다.
             * 예: "sql": "SELECT ..."
             */
            free(key);
            goto fail;
        }
        p++;
        skip_ws(&p);

        if (strcmp(key, "sql") == 0 || strcmp(key, "request_id") == 0) {
            /*
             * 우리가 실제로 사용하는 필드는 둘 다 문자열이어야 한다.
             * sql이 숫자이거나 request_id가 object면 bad request다.
             */
            parsed = parse_json_string(&p, &value);
            if (parsed <= 0) {
                free(key);
                status = parsed < 0 ? HTTP_REQUEST_ERR_INVALID_ARG
                                    : HTTP_REQUEST_ERR_BAD_REQUEST;
                goto fail;
            }

            if (strcmp(key, "sql") == 0) {
                /*
                 * 같은 key가 두 번 들어오면 마지막 값을 사용한다.
                 * 이전 값을 free하지 않으면 메모리 누수가 생긴다.
                 */
                free(sql);
                sql = value;
            } else {
                free(request_id);
                request_id = value;
            }
        } else {
            /*
             * 요구사항: unknown field는 무시한다.
             * 하지만 그냥 무시하려면 value가 어디까지인지 알아야 하므로
             * skip_json_value()로 값 하나를 정확히 건너뛴다.
             */
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
            /*
             * 콤마가 있으면 다음 key/value 쌍이 이어진다는 뜻이다.
             * 단, {"sql":"x",}처럼 마지막에 콤마만 남는 형태는 허용하지 않는다.
             */
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
    /*
     * object가 끝난 뒤에는 공백 말고 다른 글자가 있으면 안 된다.
     * 예: {"sql":"SELECT"} garbage
     */
    if (*p != '\0') goto fail;
    /*
     * sql은 필수이고 빈 문자열도 허용하지 않는다.
     */
    if (!sql || sql[0] == '\0') goto fail;
    /*
     * SQL은 반드시 한 문장이어야 한다.
     */
    if (!http_query_is_single_statement(sql)) goto fail;

    /*
     * 여기까지 왔으면 파싱 성공이다.
     * sql/request_id 포인터를 out_request에 넘겼으므로 이 함수에서는 free하지 않는다.
     */
    out_request->sql = sql;
    out_request->request_id = request_id;
    return HTTP_REQUEST_OK;

fail:
    /*
     * 실패하면 중간에 만들어 둔 문자열을 모두 정리한다.
     * out_request는 함수 시작 부분에서 이미 비워 둔 상태다.
     */
    free(sql);
    free(request_id);
    return status;
}
