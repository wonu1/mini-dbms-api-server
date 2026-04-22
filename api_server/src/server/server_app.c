#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/server_app.h"
#include "../../include/http_server.h"
#include "../../include/job_queue.h"
#include "../../include/thread_pool.h"
#include "engine_api.h"
#include "engine_runtime.h"

#define SERVER_APP_PORT_MIN 1
#define SERVER_APP_PORT_MAX 65535
#define SERVER_APP_WORKERS_MAX 1024
#define SERVER_APP_QUEUE_CAPACITY_MAX 65536

static int parse_positive_int(const char *text, int *out_value) {
    char *end = NULL;
    long value;

    if (!text || !*text || !out_value) return -1;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0') return -1;
    if (value <= 0 || value > INT_MAX) return -1;

    *out_value = (int)value;
    return 0;
}

static int assign_flag_value(const char *flag, const char *value, ServerConfig *config) {
    int parsed = 0;

    if (parse_positive_int(value, &parsed) != 0) {
        return SERVER_APP_ERR_INVALID_ARG;
    }

    if (strcmp(flag, "--port") == 0) {
        config->port = parsed;
    } else if (strcmp(flag, "--workers") == 0) {
        config->workers = parsed;
    } else if (strcmp(flag, "--queue-capacity") == 0) {
        config->queue_capacity = parsed;
    } else {
        return SERVER_APP_ERR_INVALID_ARG;
    }
    return SERVER_APP_OK;
}

void server_config_set_defaults(ServerConfig *config) {
    if (!config) return;

    config->port = API_DEFAULT_PORT;
    config->workers = API_DEFAULT_WORKERS;
    config->queue_capacity = API_DEFAULT_QUEUE_CAPACITY;
}

int server_config_validate(const ServerConfig *config) {
    if (!config) return SERVER_APP_ERR_INVALID_ARG;

    if (config->port < SERVER_APP_PORT_MIN || config->port > SERVER_APP_PORT_MAX) {
        return SERVER_APP_ERR_CONFIG;
    }
    if (config->workers <= 0 || config->workers > SERVER_APP_WORKERS_MAX) {
        return SERVER_APP_ERR_CONFIG;
    }
    if (config->queue_capacity <= 0 ||
        config->queue_capacity > SERVER_APP_QUEUE_CAPACITY_MAX) {
        return SERVER_APP_ERR_CONFIG;
    }
    return SERVER_APP_OK;
}

int server_config_parse_args(int argc, char **argv, ServerConfig *out_config) {
    int i;

    if (!argv || !out_config || argc < 1) return SERVER_APP_ERR_INVALID_ARG;

    server_config_set_defaults(out_config);

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *eq;
        int rc;

        if (!arg) return SERVER_APP_ERR_INVALID_ARG;

        eq = strchr(arg, '=');
        if (eq) {
            char flag[32];
            size_t flag_len = (size_t)(eq - arg);
            if (flag_len == 0 || flag_len >= sizeof(flag)) {
                return SERVER_APP_ERR_INVALID_ARG;
            }
            memcpy(flag, arg, flag_len);
            flag[flag_len] = '\0';
            rc = assign_flag_value(flag, eq + 1, out_config);
            if (rc != SERVER_APP_OK) return rc;
        } else {
            const char *value;
            if (i + 1 >= argc) return SERVER_APP_ERR_INVALID_ARG;
            value = argv[++i];
            if (!value) return SERVER_APP_ERR_INVALID_ARG;
            rc = assign_flag_value(arg, value, out_config);
            if (rc != SERVER_APP_OK) return rc;
        }
    }

    return server_config_validate(out_config);
}

void server_app_print_usage(const char *argv0) {
    const char *program = argv0 ? argv0 : "api_server";

    fprintf(stderr,
            "Usage: %s [--port PORT] [--workers N] [--queue-capacity N]\n"
            "  --port PORT            listening TCP port (default %d, range %d..%d)\n"
            "  --workers N            worker thread count (default %d, max %d)\n"
            "  --queue-capacity N     job queue capacity (default %d, max %d)\n",
            program,
            API_DEFAULT_PORT, SERVER_APP_PORT_MIN, SERVER_APP_PORT_MAX,
            API_DEFAULT_WORKERS, SERVER_APP_WORKERS_MAX,
            API_DEFAULT_QUEUE_CAPACITY, SERVER_APP_QUEUE_CAPACITY_MAX);
}

int server_app_run(const ServerConfig *config) {
    JobQueue queue;
    ThreadPool pool;
    int pool_inited = 0;
    int pool_started = 0;
    int queue_inited = 0;
    int engine_inited = 0;
    int http_rc;
    int exit_code = SERVER_APP_OK;

    if (!config) return SERVER_APP_ERR_INVALID_ARG;
    if (server_config_validate(config) != SERVER_APP_OK) {
        return SERVER_APP_ERR_CONFIG;
    }

    if (engine_runtime_init() != ENGINE_API_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    engine_inited = 1;

    if (engine_runtime_prepare_all() != ENGINE_API_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }

    if (job_queue_init(&queue, (size_t)config->queue_capacity) != JOB_QUEUE_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    queue_inited = 1;

    if (thread_pool_init(&pool, &queue, config->workers) != THREAD_POOL_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    pool_inited = 1;

    if (thread_pool_start(&pool) != THREAD_POOL_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
        goto cleanup;
    }
    pool_started = 1;

    http_rc = http_server_run(config, &queue);
    if (http_rc != HTTP_SERVER_OK) {
        exit_code = SERVER_APP_ERR_BOOTSTRAP;
    }

cleanup:
    if (pool_started) {
        thread_pool_stop(&pool);
    }
    if (pool_inited) {
        thread_pool_destroy(&pool);
    }
    if (queue_inited) {
        job_queue_close(&queue);
        job_queue_destroy(&queue);
    }
    if (engine_inited) {
        engine_runtime_shutdown();
    }
    return exit_code;
}
