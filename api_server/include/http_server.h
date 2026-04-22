#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "api_types.h"
#include "job_queue.h"

/*
 * HTTP server 모듈의 공개 인터페이스다.
 * server-core는 이 함수들을 통해 socket 서버를 시작하고 멈춘다.
 * 실제 HTTP 요청을 읽고 JobQueue에 넣는 구현은 http_server.c가 담당한다.
 */
typedef enum {
    /* 서버가 정상적으로 실행/종료됐다. */
    HTTP_SERVER_OK = 0,

    /* config나 queue 인자가 잘못됐다. */
    HTTP_SERVER_ERR_INVALID_ARG = -1,

    /* 예전 스텁 단계의 값이다. */
    HTTP_SERVER_ERR_NOT_IMPLEMENTED = -2,

    /* socket, bind, listen 같은 시스템 호출 실패를 뜻한다. */
    HTTP_SERVER_ERR_SYSTEM = -3
} HttpServerStatus;

/* HTTP 서버를 실행하고, 들어온 요청을 queue에 넣는다. */
int http_server_run(const ServerConfig *config, JobQueue *queue);

/* 서버에게 종료 요청이 들어왔음을 표시한다. */
void http_server_request_stop(void);

/* 종료 요청이 들어왔는지 확인한다. 1이면 종료 요청 있음, 0이면 계속 실행 가능하다. */
int http_server_stop_requested(void);

#endif /* HTTP_SERVER_H */
