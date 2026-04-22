#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include "../../include/server_app.h"
#include "../../include/http_server.h"
#include "../../include/http_response.h"
#include "../../include/job_queue.h"
#include "../../include/thread_pool.h"
#include "engine_api.h"
#include "engine_runtime.h"

#define SERVER_APP_PORT_MIN 1
#define SERVER_APP_PORT_MAX 65535
#define SERVER_APP_WORKERS_MAX 1024
#define SERVER_APP_QUEUE_CAPACITY_MAX 65536

/*
 * 이 파일은 API 서버 전체를 조립하는 부트스트랩 계층이다.
 * 설정 파싱, 엔진 초기화, queue/thread pool/http server 시작 순서를 여기서 관리한다.
 * HTTP body 파싱이나 queue 내부 구현처럼 다른 모듈의 세부 로직은 직접 다루지 않는다.
 */

/* fd socket에 data를 len 바이트만큼 전부 보낸다. */
static int server_write_all(int fd, const char *data, size_t len) {
    size_t sent = 0;

    /*
     * send()는 요청한 길이를 한 번에 모두 보내지 못할 수 있다.
     * 그래서 보낸 바이트 수(sent)를 기억하면서 남은 데이터를 끝까지 반복해서 보낸다.
     */
    while (sent < len) {
        ssize_t written = send(fd, data + sent, len - sent, 0);

        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        sent += (size_t)written;
    }

    return 0;
}

/* HTTP status code를 reason phrase 문자열로 바꾼다. 예: 200 -> OK */
static const char *server_reason_phrase(int status_code) {
    /*
     * HTTP 응답 첫 줄에는 숫자 코드와 사람이 읽는 문구가 함께 들어간다.
     * 예: HTTP/1.1 200 OK
     */
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

/* HttpResponse를 HTTP wire format으로 만들어 client socket에 전송한다. */
static int server_send_http_response(int fd, const HttpResponse *response) {
    char header[512];
    size_t body_len;
    const char *content_type;
    int header_len;

    /*
     * HttpResponse는 B 모듈이 만든 body/status/content_type 묶음이다.
     * 여기서는 그것을 실제 HTTP wire format으로 감싸 socket에 쓴다.
     */
    if (fd < 0 || !response) return -1;

    body_len = response->body ? strlen(response->body) : 0;
    content_type = response->content_type[0]
        ? response->content_type
        : "application/json";

    header_len = snprintf(header,
                          sizeof(header),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          response->status_code,
                          server_reason_phrase(response->status_code),
                          content_type,
                          body_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return -1;
    }

    if (server_write_all(fd, header, (size_t)header_len) != 0) {
        return -1;
    }

    if (body_len > 0 && server_write_all(fd, response->body, body_len) != 0) {
        return -1;
    }

    return 0;
}

/* JSON 응답 생성 실패 시에도 최소 text/plain 에러를 보낸다. */
static void server_send_minimal_error(int fd, int status_code, const char *body) {
    char response[256];
    const char *text = body ? body : "";
    int written;

    /*
     * JSON 응답을 만드는 과정 자체가 실패했을 때 쓰는 최후의 안전장치다.
     * 이 함수는 아주 단순한 text/plain 응답만 보낸다.
     */
    if (fd < 0) return;

    written = snprintf(response,
                       sizeof(response),
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n%s",
                       status_code,
                       server_reason_phrase(status_code),
                       strlen(text),
                       text);
    if (written < 0 || (size_t)written >= sizeof(response)) {
        return;
    }

    (void)server_write_all(fd, response, (size_t)written);
}

/* client fd를 닫고 -1로 바꿔 중복 close를 피한다. */
static void server_close_client_fd(int *fd) {
    /*
     * client fd를 닫은 뒤 -1로 바꿔 둔다.
     * 같은 fd를 두 번 close하는 실수를 줄이기 위한 패턴이다.
     */
    if (!fd || *fd < 0) return;

#ifdef _WIN32
    closesocket(*fd);
#else
    close(*fd);
#endif
    *fd = -1;
}

/* worker thread가 받은 QueryJob을 실행하고 HTTP 응답까지 전송한다. */
static void server_process_query_job(QueryJob *job, void *context) {
    HttpResponse http_response;
    EngineResponse engine_response;
    EngineErrorCode error_code = ENGINE_ERR_RUNTIME;
    char *error_message = NULL;
    int execute_rc;
    int status_code;
    const char *http_error_code;
    const char *message;

    /*
     * 이 함수는 worker thread가 실제 /query 작업을 처리할 때 호출된다.
     * 흐름:
     *   1. SQL이 있는지 확인한다.
     *   2. engine_execute_sql()로 DB 엔진에 넘긴다.
     *   3. 성공이면 success JSON, 실패면 error JSON을 만든다.
     *   4. 응답 전송 후 client fd와 동적 메모리를 정리한다.
     */
    (void)context;

    if (!job) return;

    http_response_init(&http_response);
    memset(&engine_response, 0, sizeof(engine_response));

    if (!job->request.sql) {
        if (http_build_error_response(400,
                                      "BAD_REQUEST",
                                      "missing SQL statement",
                                      &http_response) == HTTP_RESPONSE_OK) {
            (void)server_send_http_response(job->client_fd, &http_response);
        } else {
            server_send_minimal_error(job->client_fd, 400, "missing SQL statement");
        }
        goto cleanup;
    }

    execute_rc = engine_execute_sql(job->request.sql,
                                    &engine_response,
                                    &error_code,
                                    &error_message);
    if (execute_rc == ENGINE_API_OK) {
        if (http_build_query_success_response(&engine_response,
                                              &http_response) == HTTP_RESPONSE_OK) {
            (void)server_send_http_response(job->client_fd, &http_response);
        } else {
            server_send_minimal_error(job->client_fd,
                                      500,
                                      "failed to build query response");
        }
        goto cleanup;
    }

    status_code = http_status_from_engine_error(error_code);
    http_error_code = http_error_code_from_engine_error(error_code);
    message = error_message ? error_message : "engine request failed";

    if (http_build_error_response(status_code,
                                  http_error_code,
                                  message,
                                  &http_response) == HTTP_RESPONSE_OK) {
        (void)server_send_http_response(job->client_fd, &http_response);
    } else {
        server_send_minimal_error(job->client_fd, status_code, message);
    }

cleanup:
    engine_response_free(&engine_response);
    http_response_free(&http_response);
    free(error_message);
    server_close_client_fd(&job->client_fd);
}

/* 문자열을 양의 정수로 파싱한다. 실패하면 -1을 반환한다. */
static int parse_positive_int(const char *text, int *out_value) {
    char *end = NULL;
    long value;

    /*
     * CLI 옵션 값은 전부 양의 정수여야 한다.
     * strtol을 쓰면 "8080abc" 같은 잘못된 입력도 잡아낼 수 있다.
     */
    if (!text || !*text || !out_value) return -1;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0') return -1;
    if (value <= 0 || value > INT_MAX) return -1;

    *out_value = (int)value;
    return 0;
}

/* CLI flag 이름에 따라 value를 ServerConfig의 알맞은 필드에 넣는다. */
static int assign_flag_value(const char *flag, const char *value, ServerConfig *config) {
    int parsed = 0;

    /*
     * --port, --workers, --queue-capacity 중 어떤 옵션인지 보고
     * 파싱된 숫자를 ServerConfig의 해당 필드에 넣는다.
     */
    if (parse_positive_int(value, &parsed) != 0) {
        return SERVER_APP_ERR_INVALID_ARG;
    }

    if (strcmp(flag, "--port") == 0) {
        config->port = parsed;
    } else if (strcmp(flag, "--workers") == 0) {
        config->workers = parsed;
    } else if (strcmp(flag, "--queue-capacity") == 0) {
        config->queue_capacity = parsed;
    } else {
        return SERVER_APP_ERR_INVALID_ARG;
    }
    return SERVER_APP_OK;
}

/* ServerConfig에 기본 포트/worker/queue 값을 채운다. */
void server_config_set_defaults(ServerConfig *config) {
    /*
     * 사용자가 옵션을 하나도 주지 않아도 서버가 실행될 수 있도록 기본값을 채운다.
     */
    if (!config) return;

    config->port = API_DEFAULT_PORT;
    config->workers = API_DEFAULT_WORKERS;
    config->queue_capacity = API_DEFAULT_QUEUE_CAPACITY;
}

/* ServerConfig 값들이 허용 범위 안에 있는지 검사한다. */
int server_config_validate(const ServerConfig *config) {
    /*
     * OS와 서버가 감당할 수 있는 범위 안의 설정인지 검사한다.
     * 잘못된 설정은 서버를 시작하기 전에 막는 편이 디버깅하기 쉽다.
     */
    if (!config) return SERVER_APP_ERR_INVALID_ARG;

    if (config->port < SERVER_APP_PORT_MIN || config->port > SERVER_APP_PORT_MAX) {
        return SERVER_APP_ERR_CONFIG;
    }
    if (config->workers <= 0 || config->workers > SERVER_APP_WORKERS_MAX) {
        return SERVER_APP_ERR_CONFIG;
    }
    if (config->queue_capacity <= 0 ||
        config->queue_capacity > SERVER_APP_QUEUE_CAPACITY_MAX) {
        return SERVER_APP_ERR_CONFIG;
    }
    return SERVER_APP_OK;
}

/* argc/argv 명령행 인자를 읽어 ServerConfig를 만든다. */
int server_config_parse_args(int argc, char **argv, ServerConfig *out_config) {
    int i;

    if (!argv || !out_config || argc < 1) return SERVER_APP_ERR_INVALID_ARG;

    /*
     * 먼저 기본값을 넣고, 사용자가 넘긴 옵션만 덮어쓴다.
     * 지원하는 형태:
     *   --port=9090
     *   --port 9090
     */
    server_config_set_defaults(out_config);

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *eq;
        int rc;

        if (!arg) return SERVER_APP_ERR_INVALID_ARG;

        eq = strchr(arg, '=');
        if (eq) {
            /*
             * --port=9090 형태다.
             * '=' 앞쪽은 flag, 뒤쪽은 value로 나눈다.
             */
            char flag[32];
            size_t flag_len = (size_t)(eq - arg);
            if (flag_len == 0 || flag_len >= sizeof(flag)) {
                return SERVER_APP_ERR_INVALID_ARG;
            }
            memcpy(flag, arg, flag_len);
            flag[flag_len] = '\0';
            rc = assign_flag_value(flag, eq + 1, out_config);
            if (rc != SERVER_APP_OK) return rc;
        } else {
            /*
             * --port 9090 형태다.
             * 현재 argv가 flag이고, 바로 다음 argv가 value다.
             */
            const char *value;
            if (i + 1 >= argc) return SERVER_APP_ERR_INVALID_ARG;
            value = argv[++i];
            if (!value) return SERVER_APP_ERR_INVALID_ARG;
            rc = assign_flag_value(arg, value, out_config);
            if (rc != SERVER_APP_OK) return rc;
        }
    }

    return server_config_validate(out_config);
}

/* 사용 가능한 CLI 옵션과 기본값을 stderr에 출력한다. */
void server_app_print_usage(const char *argv0) {
    const char *program = argv0 ? argv0 : "api_server";

    fprintf(stderr,
            "Usage: %s [--port PORT] [--workers N] [--queue-capacity N]\n"
            "  --port PORT            listening TCP port (default %d, range %d..%d)\n"
            "  --workers N            worker thread count (default %d, max %d)\n"
            "  --queue-capacity N     job queue capacity (default %d, max %d)\n",
            program,
            API_DEFAULT_PORT, SERVER_APP_PORT_MIN, SERVER_APP_PORT_MAX,
            API_DEFAULT_WORKERS, SERVER_APP_WORKERS_MAX,
            API_DEFAULT_QUEUE_CAPACITY, SERVER_APP_QUEUE_CAPACITY_MAX);
}

/* 엔진, queue, thread pool, HTTP server를 순서대로 시작하고 종료 시 정리한다. */
int server_app_run(const ServerConfig *config) {
    JobQueue queue;
    ThreadPool pool;
    int pool_inited = 0;
    int pool_started = 0;
    int queue_inited = 0;
    int engine_inited = 0;
    int http_rc;
    int exit_code = SERVER_APP_OK;

    /*
     * 서버 시작 순서는 중요하다.
     * 엔진 준비 -> queue 준비 -> thread pool 준비 -> HTTP server 시작 순서로 간다.
     * 실패하면 cleanup 라벨로 이동해 이미 시작한 것들을 역순으로 정리한다.
     */
    if (!config) return SERVER_APP_ERR_INVALID_ARG;
    if (server_config_validate(config) != SERVER_APP_OK) {
        return SERVER_APP_ERR_CONFIG;
    }

    if (engine_runtime_init() != ENGINE_API_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    engine_inited = 1;

    if (engine_runtime_prepare_all() != ENGINE_API_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }

    if (job_queue_init(&queue, (size_t)config->queue_capacity) != JOB_QUEUE_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    queue_inited = 1;

    if (thread_pool_init(&pool, &queue, config->workers) != THREAD_POOL_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    pool_inited = 1;

    if (thread_pool_set_handler(&pool, server_process_query_job, NULL) != THREAD_POOL_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }

    if (thread_pool_start(&pool) != THREAD_POOL_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    pool_started = 1;

    http_rc = http_server_run(config, &queue);
    if (http_rc != HTTP_SERVER_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
    }

cleanup:
    /*
     * cleanup은 "성공 종료"와 "중간 실패" 양쪽에서 모두 사용한다.
     * 시작한 자원만 정리하기 위해 *_inited, *_started 플래그를 둔다.
     */
    if (pool_started) {
        thread_pool_stop(&pool);
    }
    if (pool_inited) {
        thread_pool_destroy(&pool);
    }
    if (queue_inited) {
        job_queue_close(&queue);
        job_queue_destroy(&queue);
    }
    if (engine_inited) {
        engine_runtime_shutdown();
    }
    return exit_code;
}
