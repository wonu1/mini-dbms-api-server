#ifndef BENCH_CLIENT_H
#define BENCH_CLIENT_H

#define BENCH_DEFAULT_HOST "127.0.0.1"
#define BENCH_DEFAULT_PORT 8080
#define BENCH_DEFAULT_WORKERS 4
#define BENCH_DEFAULT_REQUESTS 100

/*
 * bench client는 API 서버 성능을 간단히 비교하기 위한 도구다.
 * SELECT 요청만 반복할지, INSERT 요청만 반복할지 시나리오를 enum으로 구분한다.
 */
typedef enum {
    BENCH_SCENARIO_SELECT,
    BENCH_SCENARIO_INSERT
} BenchScenario;

/* bench 실행 중 발생할 수 있는 성공/실패 상태 코드다. */
typedef enum {
    BENCH_OK = 0,
    BENCH_ERR_INVALID_ARG = -1,
    BENCH_ERR_NO_MEMORY = -2,
    BENCH_ERR_RUNTIME = -3
} BenchStatus;

/*
 * bench 실행 설정이다.
 * 어떤 서버에, 몇 개 worker로, 몇 번 요청을 보낼지 담는다.
 */
typedef struct {
    const char *host;
    int port;
    /* bench client 내부 동시 요청 worker 수다. */
    int workers;
    int requests;
    BenchScenario scenario;
} BenchConfig;

/* 기본 bench 설정값을 채운다. */
void bench_config_set_defaults(BenchConfig *config);

/* 명령행 옵션을 BenchConfig로 바꾼다. */
int bench_config_parse_args(int argc, char **argv, BenchConfig *out_config);

/* enum 값을 사람이 읽기 쉬운 문자열로 바꾼다. */
const char *bench_scenario_name(BenchScenario scenario);

/* 실제 bench 요청들을 실행한다. */
int bench_run(const BenchConfig *config);

#endif /* BENCH_CLIENT_H */
