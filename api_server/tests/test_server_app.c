#include <assert.h>
#include <stddef.h>

#include "../include/server_app.h"

static void test_defaults(void) {
    ServerConfig config;
    server_config_set_defaults(&config);
    assert(config.port == API_DEFAULT_PORT);
    assert(config.workers == API_DEFAULT_WORKERS);
    assert(config.queue_capacity == API_DEFAULT_QUEUE_CAPACITY);
}

static void test_validate(void) {
    ServerConfig config;

    assert(server_config_validate(NULL) == SERVER_APP_ERR_INVALID_ARG);

    server_config_set_defaults(&config);
    assert(server_config_validate(&config) == SERVER_APP_OK);

    server_config_set_defaults(&config);
    config.port = 0;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);

    server_config_set_defaults(&config);
    config.port = 70000;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);

    server_config_set_defaults(&config);
    config.workers = 0;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);

    server_config_set_defaults(&config);
    config.queue_capacity = 0;
    assert(server_config_validate(&config) == SERVER_APP_ERR_CONFIG);
}

static void test_parse_args_mixed(void) {
    ServerConfig config;
    char *argv[] = {
        "api_server_stub",
        "--port=9090",
        "--workers",
        "8",
        "--queue-capacity",
        "128"
    };
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));

    assert(server_config_parse_args(argc, argv, &config) == SERVER_APP_OK);
    assert(config.port == 9090);
    assert(config.workers == 8);
    assert(config.queue_capacity == 128);
}

static void test_parse_args_defaults_only(void) {
    ServerConfig config;
    char *argv[] = { "api_server_stub" };

    assert(server_config_parse_args(1, argv, &config) == SERVER_APP_OK);
    assert(config.port == API_DEFAULT_PORT);
    assert(config.workers == API_DEFAULT_WORKERS);
    assert(config.queue_capacity == API_DEFAULT_QUEUE_CAPACITY);
}

static void test_parse_args_errors(void) {
    ServerConfig config;

    {
        char *argv[] = { "api_server_stub", "--port" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--unknown=1" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--port=abc" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--workers=-1" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_INVALID_ARG);
    }
    {
        char *argv[] = { "api_server_stub", "--port=70000" };
        assert(server_config_parse_args(2, argv, &config) == SERVER_APP_ERR_CONFIG);
    }
}

static void test_run_guards(void) {
    ServerConfig bad;

    assert(server_app_run(NULL) == SERVER_APP_ERR_INVALID_ARG);

    server_config_set_defaults(&bad);
    bad.port = 0;
    assert(server_app_run(&bad) == SERVER_APP_ERR_CONFIG);
}

int main(void) {
    test_defaults();
    test_validate();
    test_parse_args_mixed();
    test_parse_args_defaults_only();
    test_parse_args_errors();
    test_run_guards();
    return 0;
}
