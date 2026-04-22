#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../include/server_app.h"
#include "../../include/http_server.h"

static void on_signal(int sig) {
    (void)sig;
    http_server_request_stop();
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char **argv) {
    ServerConfig config;
    int rc;

    rc = server_config_parse_args(argc, argv, &config);
    if (rc == SERVER_APP_ERR_INVALID_ARG) {
        server_app_print_usage(argv[0]);
        return 2;
    }
    if (rc == SERVER_APP_ERR_CONFIG) {
        fprintf(stderr, "api_server: invalid configuration values\n");
        server_app_print_usage(argv[0]);
        return 2;
    }
    if (rc != SERVER_APP_OK) {
        fprintf(stderr, "api_server: failed to parse arguments (rc=%d)\n", rc);
        return 2;
    }

    install_signal_handlers();

    fprintf(stderr,
            "api_server: starting (port=%d workers=%d queue_capacity=%d)\n",
            config.port, config.workers, config.queue_capacity);

    rc = server_app_run(&config);
    if (rc == SERVER_APP_OK) return 0;
    if (rc == SERVER_APP_ERR_BOOTSTRAP) {
        fprintf(stderr, "api_server: bootstrap failed\n");
        return 1;
    }
    fprintf(stderr, "api_server: exited with code %d\n", rc);
    return 1;
}
