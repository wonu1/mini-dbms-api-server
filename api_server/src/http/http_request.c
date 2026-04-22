#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/http_request.h"

/*
 * 이 파일은 HTTP 요청의 JSON body를 우리 서버 내부 구조체로 바꾸는 일을 맡는다.
 * 외부 JSON 라이브러리를 쓰지 않기로 했기 때문에, 여기서는 우리가 필요한 만큼만
 * 직접 파싱한다. 현재 API가 필요한 값은 "sql"과 선택값 "request_id" 두 개다.
 *
 * 이 파일을 읽을 때는 "문자열 위를 손가락으로 한 칸씩 움직이며 읽는다"고 생각하면 쉽다.
 * const char **p는 그 손가락의 위치를 함수 안에서 앞으로 옮기기 위한 포인터다.
 *
 * 전체 처리 순서:
 *   1. Content-Type이 정확히 application/json인지 확인한다.
 *   2. JSON body가 { 로 시작하는지 확인한다.
 *   3. "key": value 형태를 하나씩 읽는다.
 *   4. key가 "sql"이면 SQL 문자열을 저장한다.
 *   5. key가 "request_id"이면 요청 추적용 문자열을 저장한다.
 *   6. 모르는 key는 값만 읽고 버린다.
 *   7. SQL이 한 문장인지 검사한다.
 *
 * 성공하면 out_request가 sql/request_id 문자열의 주인이 된다.
 * 실패하면 이 함수 안에서 중간에 만든 문자열을 전부 free한다.
 */

static void skip_ws(const char **p) {
    /*
     * JSON에서는 공백이 의미 없는 위치가 많다.
     * 예: {"sql":"..."} 와 { "sql" : "..." } 는 같은 JSON이다.
     *
     * 이 함수는 현재 위치가 공백이면 공백이 아닌 문자를 만날 때까지 앞으로 이동한다.
     * p가 const char **인 이유는 호출한 쪽의 포인터 위치도 실제로 바뀌어야 하기 때문이다.
     */
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static int hex_value(char c) {
    /*
     * JSON 문자열에는 \u0037 같은 unicode escape가 올 수 있다.
     * 여기서 0037은 16진수 숫자다. 이 함수는 문자 하나를 16진수 값으로 바꾼다.
     *
     * 예:
     *   '0' -> 0
     *   '9' -> 9
     *   'A' -> 10
     *   'f' -> 15
     *
     * 16진수 문자가 아니면 -1을 반환해서 "잘못된 escape"임을 알린다.
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
     * 반환값 약속:
     *   1  = 성공. *out에 malloc된 문자열을 넣는다.
     *   0  = JSON 문법이 잘못됐다.
     *  -1  = malloc 실패다.
     */
    if (**p != '"') return 0;
    (*p)++;
    s = *p;
    /*
     * escape를 해석하면 문자열이 짧아지거나 비슷한 길이가 된다.
     * 그래서 현재 위치부터 끝까지의 길이 + 1만큼 잡으면 충분하다.
     * +1은 C 문자열 끝의 '\0'을 넣기 위한 공간이다.
     */
    cap = strlen(s) + 1;
    buf = (char *)malloc(cap);
    if (!buf) return -1;

    while (**p) {
        unsigned char ch = (unsigned char)**p;

        if (ch == '"') {
            /*
             * 닫는 큰따옴표를 만나면 JSON 문자열 하나를 다 읽은 것이다.
             * p는 닫는 따옴표 다음 위치로 옮긴다.
             */
            (*p)++;
            buf[len] = '\0';
            *out = buf;
            return 1;
        }

        if (ch < 0x20) {
            /*
             * JSON 문자열 안에는 실제 줄바꿈 같은 제어문자가 그대로 들어오면 안 된다.
             * 줄바꿈은 \n처럼 escape된 형태여야 한다.
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
                     * 예:
                     *   \u0037 -> '7'
                     *   \uAC00 -> '?'  (한글 같은 비 ASCII는 여기서 완전 변환하지 않음)
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

        /*
         * escape가 아닌 일반 문자는 그대로 결과 버퍼에 복사한다.
         */
        buf[len++] = (char)ch;
        (*p)++;
    }

    /*
     * 입력이 끝났는데 닫는 큰따옴표를 못 만난 경우다.
     * 예: {"sql":"SELECT * FROM users}
     */
    free(buf);
    return 0;
}

static int skip_json_string(const char **p) {
    /*
     * unknown field의 값이 문자열이면, 문법은 정확히 확인하되 저장할 필요는 없다.
     * parse_json_string()으로 읽은 뒤 바로 free한다.
     */
    char *tmp = NULL;
    int status = parse_json_string(p, &tmp);
    free(tmp);
    return status;
}

static int skip_json_literal(const char **p) {
    const char *start = *p;

    /*
     * 숫자, true, false, null처럼 따옴표가 없는 JSON 값을 건너뛴다.
     * 콤마나 닫는 괄호를 만나면 값 하나가 끝났다고 본다.
     *
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
     * depth는 열려 있는 괄호의 개수다.
     * { 또는 [ 를 만나면 +1, } 또는 ] 를 만나면 -1 한다.
     * depth가 0이 되면 object/array 하나를 끝까지 건너뛴 것이다.
     */
    do {
        if (**p == '"') {
            /*
             * 문자열 안에 있는 {, }, [, ] 는 JSON 구조가 아니라 그냥 문자다.
             * 그래서 문자열은 통째로 건너뛰어야 depth 계산이 틀어지지 않는다.
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
     * ApiQueryRequest는 파싱된 /query 요청을 담는 구조체다.
     * 처음에는 어떤 문자열도 소유하지 않으므로 NULL로 초기화한다.
     */
    if (!request) return;

    request->sql = NULL;
    request->request_id = NULL;
}

void api_query_request_free(ApiQueryRequest *request) {
    /*
     * http_parse_query_request()가 성공하면 sql/request_id는 malloc된 문자열이다.
     * C에서는 이런 문자열을 직접 free해야 메모리 누수가 생기지 않는다.
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
     * QueryJob은 worker queue에 들어갈 작업 단위다.
     * client_fd는 아직 연결된 클라이언트가 없다는 뜻으로 -1에서 시작한다.
     */
    if (!job) return;

    job->client_fd = -1;
    api_query_request_init(&job->request);
}

void query_job_free(QueryJob *job) {
    /*
     * QueryJob 안에는 ApiQueryRequest가 들어 있다.
     * job을 정리할 때 내부 request가 가진 문자열도 함께 정리한다.
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
             * SQL 문자열 밖에서 세미콜론을 만나면 한 문장이 끝났다고 본다.
             * 이제 뒤에 다른 SQL이 더 붙어 있는지만 검사하면 된다.
             */
            seen_semicolon = 1;
            p++;
            break;
        }

        /*
         * 공백만 있는 문자열은 SQL 문장이라고 볼 수 없다.
         * 실제 글자가 하나라도 있었는지 기록한다.
         */
        if (!isspace((unsigned char)*p)) has_content = 1;
    }

    /*
     * 작은따옴표가 닫히지 않았으면 SQL 문자열이 깨진 상태다.
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
             * 예: "sql": "SELECT * FROM users"
             */
            free(key);
            goto fail;
        }
        p++;
        skip_ws(&p);

        if (strcmp(key, "sql") == 0 || strcmp(key, "request_id") == 0) {
            /*
             * 우리가 실제로 사용하는 필드는 sql과 request_id다.
             * 두 필드 모두 문자열이어야 한다.
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
                 * 같은 key가 두 번 나오면 마지막 값을 사용한다.
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
             * 하지만 값이 어디까지인지 알아야 다음 key를 읽을 수 있으므로,
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
             * 단, {"sql":"x",}처럼 마지막에 콤마만 남는 형태는 잘못된 JSON으로 본다.
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
     * JSON object가 끝난 뒤에는 공백 말고 다른 글자가 있으면 안 된다.
     * 예: {"sql":"SELECT"} garbage
     */
    if (*p != '\0') goto fail;
    /*
     * sql은 필수이며 빈 문자열도 허용하지 않는다.
     */
    if (!sql || sql[0] == '\0') goto fail;
    /*
     * SQL은 반드시 한 문장이어야 한다.
     */
    if (!http_query_is_single_statement(sql)) goto fail;

    /*
     * 여기까지 왔으면 성공이다.
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
