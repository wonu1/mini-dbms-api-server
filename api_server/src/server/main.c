#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../include/server_app.h"
#include "../../include/http_server.h"

/*
 * API 서버 실행 파일의 진입점이다.
 * main은 최대한 얇게 유지하고, 인자 파싱과 실제 서버 실행은 server_app 모듈에 맡긴다.
 * 여기서는 시그널 처리(SIGINT/SIGTERM)와 종료 코드 변환 정도만 담당한다.
 */

/* Ctrl+C(SIGINT)나 종료 신호(SIGTERM)를 받으면 HTTP 서버 종료 플래그를 켠다. */
static void on_signal(int sig) {
    (void)sig;
    http_server_request_stop();
}

/* 프로세스가 안전하게 종료될 수 있도록 필요한 signal handler를 등록한다. */
static void install_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

/* 실행 파일 시작점이다. 인자를 ServerConfig로 바꾸고 server_app_run()을 호출한다. */
int main(int argc, char **argv) {
    ServerConfig config;
    int rc;

    rc = server_config_parse_args(argc, argv, &config);
    if (rc == SERVER_APP_ERR_INVALID_ARG) {
        server_app_print_usage(argv[0]);
        return 2;
    }
    if (rc == SERVER_APP_ERR_CONFIG) {
        fprintf(stderr, "api_server: invalid configuration values\n");
        server_app_print_usage(argv[0]);
        return 2;
    }
    if (rc != SERVER_APP_OK) {
        fprintf(stderr, "api_server: failed to parse arguments (rc=%d)\n", rc);
        return 2;
    }

    install_signal_handlers();

    fprintf(stderr,
            "api_server: starting (port=%d workers=%d queue_capacity=%d)\n",
            config.port, config.workers, config.queue_capacity);

    rc = server_app_run(&config);
    if (rc == SERVER_APP_OK) return 0;
    if (rc == SERVER_APP_ERR_BOOTSTRAP) {
        fprintf(stderr, "api_server: bootstrap failed\n");
        return 1;
    }
    fprintf(stderr, "api_server: exited with code %d\n", rc);
    return 1;
}
