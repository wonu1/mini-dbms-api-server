#include <stdio.h>
#include <string.h>

#include "bench_client.h"

/* bench client는 옵션이 적어서 별도 문서 대신 usage 한 줄을 바로 보여준다. */
static void bench_print_usage(const char *argv0) {
    const char *program = argv0 ? argv0 : "bench_client";

    fprintf(stderr,
            "Usage: %s [--host HOST] [--port PORT] [--workers N] [--requests N] [--scenario select|insert]\n",
            program);
}

int main(int argc, char **argv) {
    BenchConfig config;
    int status;

    /* 도움말 확인은 서버 연결 없이 바로 빠져나간다. */
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        bench_print_usage(argv[0]);
        return 0;
    }

    /* 파싱 실패 시 usage를 보여주고 비정상 종료한다. */
    status = bench_config_parse_args(argc, argv, &config);
    if (status != BENCH_OK) {
        bench_print_usage(argv[0]);
        return 2;
    }

    /* 실제 벤치 실행 결과를 종료 코드로도 반영한다. */
    status = bench_run(&config);
    return status == BENCH_OK ? 0 : 1;
}
