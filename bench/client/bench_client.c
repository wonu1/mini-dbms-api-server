#include "bench_client.h"

void bench_config_set_defaults(BenchConfig *config) {
    if (!config) return;

    config->host = BENCH_DEFAULT_HOST;
    config->port = BENCH_DEFAULT_PORT;
    config->workers = BENCH_DEFAULT_WORKERS;
    config->requests = BENCH_DEFAULT_REQUESTS;
    config->scenario = BENCH_SCENARIO_SELECT;
}

const char *bench_scenario_name(BenchScenario scenario) {
    switch (scenario) {
        case BENCH_SCENARIO_SELECT:
            return "select";
        case BENCH_SCENARIO_INSERT:
            return "insert";
        default:
            return "unknown";
    }
}

int bench_config_parse_args(int argc, char **argv, BenchConfig *out_config) {
    if (!argv || !out_config || argc < 1) return BENCH_ERR_INVALID_ARG;

    bench_config_set_defaults(out_config);

    /* TODO: Parse bench CLI options in the implementation phase. */
    return BENCH_ERR_NOT_IMPLEMENTED;
}

int bench_run(const BenchConfig *config) {
    if (!config) return BENCH_ERR_INVALID_ARG;

    /* TODO: Implement select and insert benchmark scenarios. */
    return BENCH_ERR_NOT_IMPLEMENTED;
}
