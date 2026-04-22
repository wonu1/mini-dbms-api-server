#include "../../include/http_server.h"

static int g_stop_requested = 0;

int http_server_run(const ServerConfig *config) {
    if (!config || config->port <= 0 ||
        config->workers <= 0 || config->queue_capacity <= 0) {
        return HTTP_SERVER_ERR_INVALID_ARG;
    }

    g_stop_requested = 0;

    /* TODO: Implement the socket bind/listen/accept loop. */
    return HTTP_SERVER_ERR_NOT_IMPLEMENTED;
}

void http_server_request_stop(void) {
    g_stop_requested = 1;
}

int http_server_stop_requested(void) {
    return g_stop_requested;
}
