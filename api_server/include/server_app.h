#ifndef SERVER_APP_H
#define SERVER_APP_H

#include "api_types.h"

/*
 * server_app은 API 서버의 시작과 종료 흐름을 담당한다.
 * main()은 얇게 두고, 실제 설정 검증/부트스트랩 순서는 이 모듈에 모은다.
 */
typedef enum {
    /* 서버 앱 정상 종료 */
    SERVER_APP_OK = 0,

    /* NULL 포인터 같은 잘못된 인자 */
    SERVER_APP_ERR_INVALID_ARG = -1,

    /* port/workers/queue_capacity 설정값이 잘못됨 */
    SERVER_APP_ERR_CONFIG = -2,

    /* 엔진, queue, thread pool, HTTP server 시작 중 실패 */
    SERVER_APP_ERR_BOOTSTRAP = -3,

    /* 예전 스텁 단계의 값이다. */
    SERVER_APP_ERR_NOT_IMPLEMENTED = -4
} ServerAppStatus;

/* ServerConfig에 기본값을 채운다. */
void server_config_set_defaults(ServerConfig *config);

/* ServerConfig가 실행 가능한 값인지 검사한다. */
int server_config_validate(const ServerConfig *config);

/* 명령행 인자를 읽어서 ServerConfig를 만든다. */
int server_config_parse_args(int argc, char **argv, ServerConfig *out_config);

/* 사용법 메시지를 출력한다. */
void server_app_print_usage(const char *argv0);

/* 서버 앱 전체 실행 흐름을 시작한다. */
int server_app_run(const ServerConfig *config);

#endif /* SERVER_APP_H */
