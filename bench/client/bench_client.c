#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>

#include "bench_client.h"

typedef struct {
    const BenchConfig *config;
    int worker_id;
    /* 전체 요청을 worker별로 나눠 갖기 위한 범위 정보다. */
    int start_index;
    int request_count;
    int success_count;
    int failure_count;
} BenchWorkerArgs;

/* CLI 숫자 옵션은 모두 양의 정수만 허용한다. */
static int parse_positive_int(const char *text, int *out_value) {
    char *end = NULL;
    long value;

    if (!text || !out_value) return BENCH_ERR_INVALID_ARG;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0 || value > 2147483647L) {
        return BENCH_ERR_INVALID_ARG;
    }

    *out_value = (int)value;
    return BENCH_OK;
}

/* 사람이 읽는 문자열 시나리오를 enum으로 바꾼다. */
static int parse_scenario(const char *text, BenchScenario *out_scenario) {
    if (!text || !out_scenario) return BENCH_ERR_INVALID_ARG;

    if (strcmp(text, "select") == 0) {
        *out_scenario = BENCH_SCENARIO_SELECT;
        return BENCH_OK;
    }

    if (strcmp(text, "insert") == 0) {
        *out_scenario = BENCH_SCENARIO_INSERT;
        return BENCH_OK;
    }

    return BENCH_ERR_INVALID_ARG;
}

/* req/sec 계산을 위해 단조 증가 시계를 사용한다. */
static double bench_now_seconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

/* 각 요청은 새 TCP 연결을 열고 서버에 한 번 전송하는 단순 모델이다. */
static int connect_to_server(const char *host, int port) {
    char port_text[16];
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *cursor;
    int fd = -1;

    snprintf(port_text, sizeof(port_text), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_text, &hints, &result) != 0) {
        return -1;
    }

    for (cursor = result; cursor != NULL; cursor = cursor->ai_next) {
        fd = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (fd < 0) continue;

        if (connect(fd, cursor->ai_addr, cursor->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    return fd;
}

/* 부분 전송을 허용하지 않고 끝까지 보낼 때까지 반복한다. */
static int send_all(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t rc = send(fd, buffer + sent, length - sent, 0);
        if (rc <= 0) return -1;
        sent += (size_t)rc;
    }

    return 0;
}

/* 발표용 시나리오를 위해 select/insert SQL을 고정 포맷으로 만든다. */
static int build_sql(char *sql_buffer,
                     size_t sql_capacity,
                     BenchScenario scenario,
                     int worker_id,
                     int request_index) {
    int written;

    if (!sql_buffer || sql_capacity == 0) return BENCH_ERR_INVALID_ARG;

    if (scenario == BENCH_SCENARIO_SELECT) {
        written = snprintf(sql_buffer,
                           sql_capacity,
                           "SELECT * FROM users WHERE id = 1;");
    } else {
        written = snprintf(sql_buffer,
                           sql_capacity,
                           "INSERT INTO users (name, age, email) VALUES ('bench_%d_%d', 30, 'bench_%d_%d@example.com');",
                           worker_id,
                           request_index,
                           worker_id,
                           request_index);
    }

    if (written < 0 || (size_t)written >= sql_capacity) {
        return BENCH_ERR_RUNTIME;
    }

    return BENCH_OK;
}

/* /query HTTP 요청 하나를 만들고 결과 status code만 간단히 확인한다. */
static int perform_request(const BenchConfig *config,
                           BenchScenario scenario,
                           int worker_id,
                           int request_index) {
    char sql[512];
    char body[768];
    char request[1024];
    char response_head[256];
    int status_code = 0;
    int fd;
    int sql_status;
    int body_len;
    int request_len;
    ssize_t received;

    sql_status = build_sql(sql, sizeof(sql), scenario, worker_id, request_index);
    if (sql_status != BENCH_OK) return sql_status;

    body_len = snprintf(body, sizeof(body), "{\"sql\":\"%s\"}", sql);
    if (body_len < 0 || (size_t)body_len >= sizeof(body)) {
        return BENCH_ERR_RUNTIME;
    }

    request_len = snprintf(request,
                           sizeof(request),
                           "POST /query HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Content-Type: application/json\r\n"
                           "Connection: close\r\n"
                           "Content-Length: %d\r\n"
                           "\r\n"
                           "%s",
                           config->host,
                           config->port,
                           body_len,
                           body);
    if (request_len < 0 || (size_t)request_len >= sizeof(request)) {
        return BENCH_ERR_RUNTIME;
    }

    fd = connect_to_server(config->host, config->port);
    if (fd < 0) return BENCH_ERR_RUNTIME;

    if (send_all(fd, request, (size_t)request_len) != 0) {
        close(fd);
        return BENCH_ERR_RUNTIME;
    }

    memset(response_head, 0, sizeof(response_head));
    received = recv(fd, response_head, sizeof(response_head) - 1U, 0);
    close(fd);

    if (received <= 0) return BENCH_ERR_RUNTIME;

    if (sscanf(response_head, "HTTP/%*s %d", &status_code) != 1) {
        return BENCH_ERR_RUNTIME;
    }

    return (status_code >= 200 && status_code < 300)
        ? BENCH_OK
        : BENCH_ERR_RUNTIME;
}

/* bench worker 하나가 맡은 요청 수만큼 반복해서 서버를 두드린다. */
static void *bench_worker_main(void *arg) {
    BenchWorkerArgs *worker = (BenchWorkerArgs *)arg;
    int offset;

    worker->success_count = 0;
    worker->failure_count = 0;

    for (offset = 0; offset < worker->request_count; offset++) {
        int status = perform_request(worker->config,
                                     worker->config->scenario,
                                     worker->worker_id,
                                     worker->start_index + offset);
        if (status == BENCH_OK) {
            worker->success_count++;
        } else {
            worker->failure_count++;
        }
    }

    return NULL;
}

void bench_config_set_defaults(BenchConfig *config) {
    if (!config) return;

    config->host = BENCH_DEFAULT_HOST;
    config->port = BENCH_DEFAULT_PORT;
    config->workers = BENCH_DEFAULT_WORKERS;
    config->requests = BENCH_DEFAULT_REQUESTS;
    config->scenario = BENCH_SCENARIO_SELECT;
}

const char *bench_scenario_name(BenchScenario scenario) {
    switch (scenario) {
        case BENCH_SCENARIO_SELECT:
            return "select";
        case BENCH_SCENARIO_INSERT:
            return "insert";
        default:
            return "unknown";
    }
}

int bench_config_parse_args(int argc, char **argv, BenchConfig *out_config) {
    int i;

    if (!argv || !out_config || argc < 1) return BENCH_ERR_INVALID_ARG;

    bench_config_set_defaults(out_config);

    /* 긴 옵션만 지원해 파서를 단순하게 유지한다. */
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--host") == 0 && i + 1 < argc) {
            out_config->host = argv[++i];
        } else if (strcmp(arg, "--port") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], &out_config->port) != BENCH_OK) {
                return BENCH_ERR_INVALID_ARG;
            }
        } else if (strcmp(arg, "--workers") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], &out_config->workers) != BENCH_OK) {
                return BENCH_ERR_INVALID_ARG;
            }
        } else if (strcmp(arg, "--requests") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], &out_config->requests) != BENCH_OK) {
                return BENCH_ERR_INVALID_ARG;
            }
        } else if (strcmp(arg, "--scenario") == 0 && i + 1 < argc) {
            if (parse_scenario(argv[++i], &out_config->scenario) != BENCH_OK) {
                return BENCH_ERR_INVALID_ARG;
            }
        } else {
            return BENCH_ERR_INVALID_ARG;
        }
    }

    return BENCH_OK;
}

int bench_run(const BenchConfig *config) {
    BenchWorkerArgs *workers = NULL;
    pthread_t *threads = NULL;
    int worker_count;
    int base_requests;
    int remainder;
    int start_index;
    int total_success = 0;
    int total_failure = 0;
    int i;
    double start_time;
    double elapsed;

    if (!config) return BENCH_ERR_INVALID_ARG;
    if (!config->host || config->port <= 0 ||
        config->workers <= 0 || config->requests <= 0) {
        return BENCH_ERR_INVALID_ARG;
    }

    /* 전체 요청 수를 worker 수에 맞게 최대한 균등하게 나눈다. */
    worker_count = config->workers;
    workers = (BenchWorkerArgs *)calloc((size_t)worker_count, sizeof(BenchWorkerArgs));
    threads = (pthread_t *)calloc((size_t)worker_count, sizeof(pthread_t));
    if (!workers || !threads) {
        free(workers);
        free(threads);
        return BENCH_ERR_NO_MEMORY;
    }

    base_requests = config->requests / worker_count;
    remainder = config->requests % worker_count;
    start_index = 0;
    start_time = bench_now_seconds();

    for (i = 0; i < worker_count; i++) {
        workers[i].config = config;
        workers[i].worker_id = i;
        workers[i].request_count = base_requests + (i < remainder ? 1 : 0);
        workers[i].start_index = start_index;
        start_index += workers[i].request_count;

        if (pthread_create(&threads[i], NULL, bench_worker_main, &workers[i]) != 0) {
            int joined;
            for (joined = 0; joined < i; joined++) {
                pthread_join(threads[joined], NULL);
            }
            free(workers);
            free(threads);
            return BENCH_ERR_RUNTIME;
        }
    }

    for (i = 0; i < worker_count; i++) {
        pthread_join(threads[i], NULL);
        total_success += workers[i].success_count;
        total_failure += workers[i].failure_count;
    }

    /* 전체 wall-clock time 기준으로 처리량을 계산한다. */
    elapsed = bench_now_seconds() - start_time;
    if (elapsed <= 0.0) elapsed = 0.000001;

    printf("scenario=%s workers=%d requests=%d success=%d failure=%d elapsed=%.3fs req_per_sec=%.2f\n",
           bench_scenario_name(config->scenario),
           config->workers,
           config->requests,
           total_success,
           total_failure,
           elapsed,
           (double)config->requests / elapsed);

    free(workers);
    free(threads);
    return total_failure == 0 ? BENCH_OK : BENCH_ERR_RUNTIME;
}
