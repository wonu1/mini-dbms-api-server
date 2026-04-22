#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "api_types.h"

typedef enum {
    HTTP_SERVER_OK = 0,
    HTTP_SERVER_ERR_INVALID_ARG = -1,
    HTTP_SERVER_ERR_NOT_IMPLEMENTED = -2
} HttpServerStatus;

int http_server_run(const ServerConfig *config);
void http_server_request_stop(void);
int http_server_stop_requested(void);

#endif /* HTTP_SERVER_H */
