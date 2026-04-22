#include <stdio.h>

#include "../../include/server_app.h"

void server_config_set_defaults(ServerConfig *config) {
    if (!config) return;

    config->port = API_DEFAULT_PORT;
    config->workers = API_DEFAULT_WORKERS;
    config->queue_capacity = API_DEFAULT_QUEUE_CAPACITY;
}

int server_config_validate(const ServerConfig *config) {
    if (!config) return SERVER_APP_ERR_INVALID_ARG;

    /* TODO: Define validation rules for port, workers, and queue size. */
    return SERVER_APP_ERR_NOT_IMPLEMENTED;
}

int server_config_parse_args(int argc, char **argv, ServerConfig *out_config) {
    if (!argv || !out_config || argc < 1) return SERVER_APP_ERR_INVALID_ARG;

    server_config_set_defaults(out_config);

    /* TODO: Parse --port, --workers, and --queue-capacity. */
    return SERVER_APP_ERR_NOT_IMPLEMENTED;
}

void server_app_print_usage(const char *argv0) {
    const char *program = argv0 ? argv0 : "api_server";

    fprintf(stderr,
            "Usage: %s [--port PORT] [--workers N] [--queue-capacity N]\n",
            program);
}

int server_app_run(const ServerConfig *config) {
    if (!config) return SERVER_APP_ERR_INVALID_ARG;

    /* TODO: Wire runtime init, prepare, thread pool, and HTTP server startup. */
    return SERVER_APP_ERR_NOT_IMPLEMENTED;
}
