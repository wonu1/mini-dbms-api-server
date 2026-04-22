#ifndef BENCH_CLIENT_H
#define BENCH_CLIENT_H

#define BENCH_DEFAULT_HOST "127.0.0.1"
#define BENCH_DEFAULT_PORT 8080
#define BENCH_DEFAULT_WORKERS 4
#define BENCH_DEFAULT_REQUESTS 100

typedef enum {
    BENCH_SCENARIO_SELECT,
    BENCH_SCENARIO_INSERT
} BenchScenario;

typedef enum {
    BENCH_OK = 0,
    BENCH_ERR_INVALID_ARG = -1,
    BENCH_ERR_NOT_IMPLEMENTED = -2
} BenchStatus;

typedef struct {
    const char *host;
    int port;
    int workers;
    int requests;
    BenchScenario scenario;
} BenchConfig;

void bench_config_set_defaults(BenchConfig *config);
int bench_config_parse_args(int argc, char **argv, BenchConfig *out_config);
const char *bench_scenario_name(BenchScenario scenario);
int bench_run(const BenchConfig *config);

#endif /* BENCH_CLIENT_H */
