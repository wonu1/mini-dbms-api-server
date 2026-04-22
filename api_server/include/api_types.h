#ifndef API_TYPES_H
#define API_TYPES_H

#define API_DEFAULT_PORT 8080
#define API_DEFAULT_WORKERS 4
#define API_DEFAULT_QUEUE_CAPACITY 64

/* API 서버 전체에서 공유하는 최소 설정값이다. */
typedef struct {
    int port;
    int workers;
    int queue_capacity;
} ServerConfig;

/* /query 요청 본문을 서버 내부 표현으로 옮긴다. */
typedef struct {
    char *sql;
    char *request_id;
} ApiQueryRequest;

/* worker 큐가 소유하는 요청 단위다. */
typedef struct {
    int client_fd;
    ApiQueryRequest request;
} QueryJob;

#endif /* API_TYPES_H */
