#include <assert.h>

#include "../include/server_app.h"

int main(void) {
    ServerConfig config;
    char *argv[] = {
        "api_server_stub",
        "--port=9090",
        "--workers",
        "8",
        "--queue-capacity",
        "128"
    };

    server_config_set_defaults(&config);
    assert(config.port == API_DEFAULT_PORT);
    assert(config.workers == API_DEFAULT_WORKERS);
    assert(config.queue_capacity == API_DEFAULT_QUEUE_CAPACITY);

    assert(server_config_validate(&config) == SERVER_APP_ERR_NOT_IMPLEMENTED);
    assert(server_config_parse_args(6, argv, &config) == SERVER_APP_ERR_NOT_IMPLEMENTED);
    assert(server_app_run(&config) == SERVER_APP_ERR_NOT_IMPLEMENTED);

    return 0;
}
