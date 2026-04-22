#ifndef SERVER_APP_H
#define SERVER_APP_H

#include "api_types.h"

typedef enum {
    SERVER_APP_OK = 0,
    SERVER_APP_ERR_INVALID_ARG = -1,
    SERVER_APP_ERR_CONFIG = -2,
    SERVER_APP_ERR_BOOTSTRAP = -3,
    SERVER_APP_ERR_NOT_IMPLEMENTED = -4
} ServerAppStatus;

void server_config_set_defaults(ServerConfig *config);
int server_config_validate(const ServerConfig *config);
int server_config_parse_args(int argc, char **argv, ServerConfig *out_config);
void server_app_print_usage(const char *argv0);
int server_app_run(const ServerConfig *config);

#endif /* SERVER_APP_H */
