#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../include/http_server.h"
#include "../include/server_app.h"

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(path) _mkdir(path)
#else
#  define MKDIR(path) mkdir(path, 0755)
#endif

/*
 * server_app 통합 성격의 테스트다.
 * 설정 파싱/검증뿐 아니라 실제 HTTP server를 테스트 스레드에서 띄우고
 * /health, /query 요청이 동작하는지 확인한다.
 */

#define TEST_QUERY_TABLE "server_app_query_users"
#define TEST_SCHEMA_PATH "db_engine/schema/" TEST_QUERY_TABLE ".schema"
#define TEST_DATA_PATH "db_engine/data/" TEST_QUERY_TABLE ".dat"
#define TEST_LOOPBACK_ADDR 0x7f000001u

typedef struct {
    ServerConfig config;
    int result;
} ServerThreadArgs;

static void remove_if_exists(const char *path) {
    if (!path) return;
    remove(path);
}

static void write_text_file(const char *path, const char *contents) {
    FILE *fp = fopen(path, "wb");

    assert(fp != NULL);
    fputs(contents, fp);
    fclose(fp);
}

static void setup_query_fixture(void) {
    MKDIR("db_engine");
    MKDIR("db_engine/schema");
    MKDIR("db_engine/data");

    remove_if_exists(TEST_SCHEMA_PATH);
    remove_if_exists(TEST_DATA_PATH);

    write_text_file(TEST_SCHEMA_PATH,
                    "table=" TEST_QUERY_TABLE "\n"
                    "columns=4\n"
                    "col0=id,INT,0,PK,AUTO_INCREMENT\n"
                    "col1=name,VARCHAR,64\n"
                    "col2=age,INT,0\n"
                    "col3=email,VARCHAR,128\n");
    write_text_file(TEST_DATA_PATH,
                    "1 | alice | 25 | alice@example.com\n"
                    "2 | bob | 32 | bob@example.com\n");
}

static void cleanup_query_fixture(void) {
    remove_if_exists(TEST_SCHEMA_PATH);
    remove_if_exists(TEST_DATA_PATH);
}

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;

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

static int pick_free_port(void) {
    int fd;
    int port;
    struct sockaddr_in addr;
    socklen_t addr_len = (socklen_t)sizeof(addr);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(TEST_LOOPBACK_ADDR);
    addr.sin_port = 0;

    assert(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(getsockname(fd, (struct sockaddr *)&addr, &addr_len) == 0);

    port = (int)ntohs(addr.sin_port);
    close(fd);
    return port;
}

static int perform_request(int port,
                           const char *request,
                           char *response,
                           size_t response_size) {
    int fd;
    size_t total = 0;
    struct sockaddr_in addr;

    if (!request || !response || response_size == 0) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(TEST_LOOPBACK_ADDR);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    if (send_all(fd, request, strlen(request)) != 0) {
        close(fd);
        return -1;
    }

    while (total + 1 < response_size) {
        ssize_t read_size = recv(fd,
                                 response + total,
                                 response_size - total - 1,
                                 0);

        if (read_size < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }

        if (read_size == 0) break;
        total += (size_t)read_size;
    }

    response[total] = '\0';
    close(fd);
    return 0;
}

static void wait_for_server_ready(int port) {
    const char request[] =
        "GET /health HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    char response[1024];
    int attempt;
    const struct timespec retry_delay = {0, 100000000};

    for (attempt = 0; attempt < 50; attempt++) {
        if (perform_request(port, request, response, sizeof(response)) == 0 &&
            strstr(response, "HTTP/1.1 200 OK") != NULL) {
            return;
        }

        nanosleep(&retry_delay, NULL);
    }

    assert(!"server did not become ready in time");
}

static void *server_thread_main(void *arg) {
    ServerThreadArgs *thread_args = (ServerThreadArgs *)arg;

    thread_args->result = server_app_run(&thread_args->config);
    return NULL;
}

static void test_defaults(void) {
    ServerConfig config;
    server_config_set_defaults(&config);
    assert(config.port == API_DEFAULT_PORT);
    assert(config.workers == API_DEFAULT_WORKERS);
    assert(config.queue_capacity == API_DEFAULT_QUEUE_CAPACITY);
}

static void test_validate(void) {
    ServerConfig config;

    assert(server_config_validate(NULL) == SERVER_APP_ERR_INVALID_ARG);

    server_config_set_defaults(&config);
    assert(server_config_validate(&config) == SERVER_APP_OK);

    server_config_set_defaults(&config);
    config.port = 0;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);

    server_config_set_defaults(&config);
    config.port = 70000;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);

    server_config_set_defaults(&config);
    config.workers = 0;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);

    server_config_set_defaults(&config);
    config.queue_capacity = 0;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);
}

static void test_parse_args_mixed(void) {
    ServerConfig config;
    char *argv[] = {
        "api_server_stub",
        "--port=9090",
        "--workers",
        "8",
        "--queue-capacity",
        "128"
    };
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));

    assert(server_config_parse_args(argc, argv, &config) == SERVER_APP_OK);
    assert(config.port == 9090);
    assert(config.workers == 8);
    assert(config.queue_capacity == 128);
}

static void test_parse_args_defaults_only(void) {
    ServerConfig config;
    char *argv[] = { "api_server_stub" };

    assert(server_config_parse_args(1, argv, &config) == SERVER_APP_OK);
    assert(config.port == API_DEFAULT_PORT);
    assert(config.workers == API_DEFAULT_WORKERS);
    assert(config.queue_capacity == API_DEFAULT_QUEUE_CAPACITY);
}

static void test_parse_args_errors(void) {
    ServerConfig config;

    {
        char *argv[] = { "api_server_stub", "--port" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--unknown=1" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--port=abc" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--workers=-1" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--port=70000" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_CONFIG);
    }
}

static void test_run_guards(void) {
    ServerConfig bad;

    assert(server_app_run(NULL) == SERVER_APP_ERR_INVALID_ARG);

    server_config_set_defaults(&bad);
    bad.port = 0;
    assert(server_app_run(&bad) == SERVER_APP_ERR_CONFIG);
}

static void test_query_end_to_end(void) {
    const char *valid_body =
        "{\"sql\":\"SELECT id, name FROM " TEST_QUERY_TABLE
        " WHERE id BETWEEN 1 AND 2;\",\"request_id\":\"req-1\"}";
    const char *invalid_body =
        "{\"sql\":\"SELECT FROM " TEST_QUERY_TABLE
        ";\",\"request_id\":\"req-2\"}";
    char request[1024];
    char response[8192];
    ServerThreadArgs thread_args;
    pthread_t thread;

    setup_query_fixture();

    server_config_set_defaults(&thread_args.config);
    thread_args.config.port = pick_free_port();
    thread_args.config.workers = 2;
    thread_args.config.queue_capacity = 8;
    thread_args.result = SERVER_APP_ERR_BOOTSTRAP;

    assert(pthread_create(&thread, NULL, server_thread_main, &thread_args) == 0);
    wait_for_server_ready(thread_args.config.port);

    snprintf(request,
             sizeof(request),
             "POST /query HTTP/1.1\r\n"
             "Host: 127.0.0.1\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n%s",
             strlen(valid_body),
             valid_body);
    assert(perform_request(thread_args.config.port,
                           request,
                           response,
                           sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "\"request_id\":\"req-1\"") != NULL);
    assert(strstr(response, "\"columns\":[\"id\",\"name\"]") != NULL);
    assert(strstr(response, "\"rows\":[[\"1\",\"alice\"],[\"2\",\"bob\"]]") != NULL);

    snprintf(request,
             sizeof(request),
             "POST /query HTTP/1.1\r\n"
             "Host: 127.0.0.1\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n%s",
             strlen(invalid_body),
             invalid_body);
    assert(perform_request(thread_args.config.port,
                           request,
                           response,
                           sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(strstr(response, "\"request_id\":\"req-2\"") != NULL);
    assert(strstr(response, "\"code\":\"PARSE_ERROR\"") != NULL);

    http_server_request_stop();
    assert(pthread_join(thread, NULL) == 0);
    assert(thread_args.result == SERVER_APP_OK);

    cleanup_query_fixture();
}

int main(void) {
    test_defaults();
    test_validate();
    test_parse_args_mixed();
    test_parse_args_defaults_only();
    test_parse_args_errors();
    test_run_guards();
    test_query_end_to_end();
    return 0;
}
