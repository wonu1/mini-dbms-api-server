#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../include/http_server.h"
#include "../../include/http_request.h"
#include "../../include/http_response.h"
#include "../../include/job_queue.h"

#define HTTP_READ_BUFFER 8192
#define HTTP_LISTEN_BACKLOG 64
#define HTTP_ACCEPT_TIMEOUT_US 500000
#define HTTP_CLIENT_RECV_TIMEOUT_SEC 5

/*
 * 이 파일은 실제 TCP socket 기반 HTTP 서버 구현이다.
 * 클라이언트 연결을 받고, HTTP 요청의 시작줄/헤더/body를 읽은 뒤,
 * /health는 바로 응답하고 /query는 QueryJob으로 만들어 worker queue에 넣는다.
 */

static volatile sig_atomic_t g_stop_requested = 0;

/* 서버 종료 요청 플래그를 켠다. SIGINT/SIGTERM 처리나 테스트에서 호출한다. */
void http_server_request_stop(void) {
    g_stop_requested = 1;
}

/* 서버가 종료 요청을 받았는지 확인한다. accept loop가 이 값을 보고 빠져나간다. */
int http_server_stop_requested(void) {
    return g_stop_requested != 0;
}

/* send()가 일부만 보낼 수 있으므로, 지정한 길이만큼 끝까지 반복해서 전송한다. */
static int write_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* HTTP 상태 코드 숫자를 응답 첫 줄에 들어갈 짧은 문구로 바꾼다. */
static const char *reason_phrase(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

/* HttpResponse 구조체를 실제 HTTP 응답 헤더와 body로 만들어 client socket에 쓴다. */
static int send_http_response(int fd, const HttpResponse *response) {
    char header[512];
    size_t body_len = response->body ? strlen(response->body) : 0;
    const char *ctype = response->content_type[0] ? response->content_type : "application/json";
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        response->status_code,
                        reason_phrase(response->status_code),
                        ctype,
                        body_len);
    if (hlen < 0 || (size_t)hlen >= sizeof(header)) return -1;
    if (write_all(fd, header, (size_t)hlen) != 0) return -1;
    if (body_len > 0 && write_all(fd, response->body, body_len) != 0) return -1;
    return 0;
}

/* JSON 응답 생성이 실패했을 때도 최소한의 text/plain 에러 응답을 보낸다. */
static int send_minimal_error(int fd, int status_code, const char *body) {
    char buf[256];
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n%s",
                     status_code, reason_phrase(status_code),
                     body_len, body ? body : "");
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    return write_all(fd, buf, (size_t)n);
}

/* 에러 코드와 메시지로 JSON 에러 응답을 만들고 client에게 보낸다. */
static void send_error(int fd, int status_code, const char *error_code, const char *message) {
    HttpResponse response;
    http_response_init(&response);
    if (http_build_error_response(status_code, error_code, message, &response) == HTTP_RESPONSE_OK) {
        send_http_response(fd, &response);
    } else {
        send_minimal_error(fd, status_code, message ? message : error_code);
    }
    http_response_free(&response);
}

/* HTTP header 블록에서 원하는 header 이름의 값을 찾아 out에 복사한다. */
static int extract_header(const char *headers, size_t len,
                          const char *name, char *out, size_t out_size) {
    size_t name_len = strlen(name);
    const char *p = headers;
    const char *end = headers + len;

    if (!out || out_size == 0) return -1;
    out[0] = '\0';

    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        size_t line_len;
        if (!line_end) break;
        line_len = (size_t)(line_end - p);
        if (line_len > 0 && p[line_len - 1] == '\r') line_len--;
        if (line_len > name_len + 1 &&
            strncasecmp(p, name, name_len) == 0 &&
            p[name_len] == ':') {
            const char *val = p + name_len + 1;
            const char *val_end = p + line_len;
            size_t val_len;
            while (val < val_end && (*val == ' ' || *val == '\t')) val++;
            val_len = (size_t)(val_end - val);
            if (val_len >= out_size) val_len = out_size - 1;
            memcpy(out, val, val_len);
            out[val_len] = '\0';
            return 0;
        }
        p = line_end + 1;
    }
    return -1;
}

/* client socket에서 HTTP 요청을 읽고, header 끝 위치까지 찾아낸다. */
static int read_request(int fd, char *buf, size_t cap, size_t *out_total, size_t *out_header_end) {
    size_t total = 0;
    while (total < cap - 1) {
        ssize_t n;
        char *terminator;
        n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        total += (size_t)n;
        buf[total] = '\0';
        terminator = strstr(buf, "\r\n\r\n");
        if (terminator) {
            *out_header_end = (size_t)(terminator - buf) + 4;
            *out_total = total;
            return 0;
        }
    }
    return -1;
}

/* Content-Length만큼 body가 아직 덜 읽혔다면 나머지 body를 추가로 읽는다. */
static int ensure_body(int fd, char *buf, size_t cap, size_t header_end,
                      size_t content_length, size_t *out_total) {
    size_t total = *out_total;
    while (total - header_end < content_length) {
        ssize_t n;
        if (total >= cap - 1) return -1;
        n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    buf[total] = '\0';
    *out_total = total;
    return 0;
}

/* GET /health 요청을 처리한다. DB 작업 없이 바로 {"status":"ok"}를 보낸다. */
static void handle_get_health(int fd) {
    HttpResponse response;
    http_response_init(&response);
    if (http_build_health_response(&response) == HTTP_RESPONSE_OK) {
        send_http_response(fd, &response);
    } else {
        send_minimal_error(fd, 500, "health not available");
    }
    http_response_free(&response);
}

/* POST /query 요청의 header/body를 검증하고 QueryJob으로 만들어 worker queue에 넣는다. */
static void handle_post_query(int fd, JobQueue *queue,
                              char *buf, size_t cap, size_t total, size_t header_end) {
    char content_type[128] = {0};
    char content_length_str[32] = {0};
    long content_length = 0;
    QueryJob job;
    int push_rc;

    extract_header(buf, header_end, "Content-Type", content_type, sizeof(content_type));
    extract_header(buf, header_end, "Content-Length", content_length_str, sizeof(content_length_str));

    if (content_length_str[0]) {
        content_length = strtol(content_length_str, NULL, 10);
        if (content_length < 0) {
            send_error(fd, 400, "BAD_REQUEST", "negative Content-Length");
            close(fd);
            return;
        }
    }

    if ((size_t)content_length >= cap - header_end) {
        send_error(fd, 413, "PAYLOAD_TOO_LARGE", "request body exceeds server limit");
        close(fd);
        return;
    }

    if (content_length > 0 &&
        ensure_body(fd, buf, cap, header_end, (size_t)content_length, &total) != 0) {
        send_error(fd, 400, "BAD_REQUEST", "body truncated");
        close(fd);
        return;
    }
    buf[header_end + (size_t)content_length] = '\0';

    query_job_init(&job);
    if (http_parse_query_request(content_type, buf + header_end, &job.request) != HTTP_REQUEST_OK) {
        send_error(fd, 400, "BAD_REQUEST", "invalid query payload");
        query_job_free(&job);
        close(fd);
        return;
    }

    job.client_fd = fd;
    push_rc = job_queue_push(queue, &job);
    if (push_rc == JOB_QUEUE_OK) {
        /* Worker thread now owns fd and job memory. */
        return;
    }

    if (push_rc == JOB_QUEUE_ERR_FULL) {
        send_error(fd, 503, "QUEUE_FULL", "server overloaded");
    } else if (push_rc == JOB_QUEUE_ERR_CLOSED) {
        send_error(fd, 503, "SHUTTING_DOWN", "server is shutting down");
    } else {
        send_error(fd, 500, "ENQUEUE_FAILED", "failed to enqueue job");
    }
    query_job_free(&job);
    close(fd);
}

/* 연결 하나를 읽어서 route별 처리 함수로 나눈다. 지원 route는 GET /health, POST /query다. */
static void handle_connection(int fd, JobQueue *queue) {
    char buf[HTTP_READ_BUFFER];
    size_t total = 0;
    size_t header_end = 0;
    char method[16] = {0};
    char path[256] = {0};
    struct timeval tv;

    tv.tv_sec = HTTP_CLIENT_RECV_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (read_request(fd, buf, sizeof(buf), &total, &header_end) != 0) {
        send_minimal_error(fd, 400, "malformed HTTP request");
        close(fd);
        return;
    }

    if (sscanf(buf, "%15s %255s", method, path) != 2) {
        send_error(fd, 400, "BAD_REQUEST", "invalid request line");
        close(fd);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
        handle_get_health(fd);
        close(fd);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/query") == 0) {
        handle_post_query(fd, queue, buf, sizeof(buf), total, header_end);
        return;
    }

    if (strcmp(method, "GET") != 0 &&
        strcmp(method, "POST") != 0) {
        send_error(fd, 405, "METHOD_NOT_ALLOWED", "method not allowed");
    } else {
        send_error(fd, 404, "NOT_FOUND", "unknown route");
    }
    close(fd);
}

/* listen socket을 만들고 port에 bind/listen까지 완료한 fd를 반환한다. */
static int create_listen_socket(int port) {
    int fd;
    int one = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, HTTP_LISTEN_BACKLOG) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* HTTP 서버의 메인 루프다. listen socket을 열고, 연결을 accept해서 handle_connection에 넘긴다. */
int http_server_run(const ServerConfig *config, JobQueue *queue) {
    int listen_fd;

    if (!config || !queue || config->port <= 0 ||
        config->workers <= 0 || config->queue_capacity <= 0) {
        return HTTP_SERVER_ERR_INVALID_ARG;
    }

    g_stop_requested = 0;

    listen_fd = create_listen_socket(config->port);
    if (listen_fd < 0) {
        fprintf(stderr, "http_server: failed to bind port %d: %s\n",
                config->port, strerror(errno));
        return HTTP_SERVER_ERR_SYSTEM;
    }

    fprintf(stderr, "http_server: listening on 0.0.0.0:%d\n", config->port);

    while (!http_server_stop_requested()) {
        fd_set rset;
        struct timeval tv;
        int ready;
        int client_fd;
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        FD_ZERO(&rset);
        FD_SET(listen_fd, &rset);
        tv.tv_sec = 0;
        tv.tv_usec = HTTP_ACCEPT_TIMEOUT_US;

        ready = select(listen_fd + 1, &rset, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "http_server: select failed: %s\n", strerror(errno));
            break;
        }
        if (ready == 0) continue;

        client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            fprintf(stderr, "http_server: accept failed: %s\n", strerror(errno));
            continue;
        }

        handle_connection(client_fd, queue);
    }

    close(listen_fd);
    fprintf(stderr, "http_server: stopped\n");
    return HTTP_SERVER_OK;
}
