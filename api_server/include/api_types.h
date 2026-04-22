#ifndef API_TYPES_H
#define API_TYPES_H

#define API_DEFAULT_PORT 8080
#define API_DEFAULT_WORKERS 4
#define API_DEFAULT_QUEUE_CAPACITY 64

/*
 * API 서버 전체에서 공유하는 기본 설정값이다.
 * main/server 쪽에서 별도 옵션을 받지 않으면 이 값을 기본으로 사용한다.
 */
typedef struct {
    /* 서버가 listen할 TCP 포트 번호다. 예: 8080 */
    int port;

    /* 동시에 요청을 처리할 worker thread 개수다. */
    int workers;

    /* worker가 바쁠 때 요청을 잠시 쌓아둘 queue 크기다. */
    int queue_capacity;
} ServerConfig;

/*
 * /query 요청 본문을 파싱한 결과다.
 * 클라이언트 JSON:
 *   {"sql":"SELECT * FROM users;"}
 *
 * 파싱 후:
 *   sql -> "SELECT * FROM users;"
 *
 * sql은 malloc된 문자열이므로 api_query_request_free()로 정리한다.
 */
typedef struct {
    char *sql;
} ApiQueryRequest;

/*
 * worker queue에 들어가는 작업 한 개를 뜻한다.
 * HTTP server가 client_fd와 파싱된 request를 묶어 queue에 넣으면,
 * worker thread가 꺼내서 엔진 실행과 응답 전송을 이어서 처리한다.
 */
typedef struct {
    /* 클라이언트와 연결된 socket 파일 디스크립터다. 아직 없으면 -1을 쓴다. */
    int client_fd;

    /* 이 client가 보낸 SQL 요청 내용이다. */
    ApiQueryRequest request;
} QueryJob;

#endif /* API_TYPES_H */
