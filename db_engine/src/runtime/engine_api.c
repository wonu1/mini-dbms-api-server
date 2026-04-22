#include <stdlib.h>
#include <string.h>

#include "../../include/engine_api.h"

static char *dup_string(const char *src) {
    size_t len;
    char *copy;

    if (!src) return NULL;

    len = strlen(src) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;

    memcpy(copy, src, len);
    return copy;
}

const char *engine_error_code_name(EngineErrorCode code) {
    switch (code) {
        case ENGINE_ERR_PARSE:
            return "ENGINE_ERR_PARSE";
        case ENGINE_ERR_VALIDATION:
            return "ENGINE_ERR_VALIDATION";
        case ENGINE_ERR_UNSUPPORTED:
            return "ENGINE_ERR_UNSUPPORTED";
        case ENGINE_ERR_RUNTIME:
            return "ENGINE_ERR_RUNTIME";
        case ENGINE_ERR_NOT_IMPLEMENTED:
        default:
            return "ENGINE_ERR_NOT_IMPLEMENTED";
    }
}

int engine_execute_sql(const char *sql,
                       EngineResponse *out,
                       EngineErrorCode *err_code,
                       char **err_message) {
    if (!sql || !out || !err_code || !err_message) {
        return ENGINE_API_ERR;
    }

    memset(out, 0, sizeof(*out));
    *err_code = ENGINE_ERR_NOT_IMPLEMENTED;
    *err_message = dup_string(
        "engine_execute_sql is not implemented yet. "
        "Use this interface as the server-engine boundary.");

    /* TODO: Implement parse, validate, lock, and execute flow here. */
    return ENGINE_API_ERR;
}

void engine_response_free(EngineResponse *res) {
    int row_index;
    int col_index;

    if (!res) return;

    if (res->type == ENGINE_RESULT_SELECT) {
        if (res->select.rows) {
            for (row_index = 0; row_index < res->select.row_count; row_index++) {
                if (!res->select.rows[row_index]) continue;
                for (col_index = 0; col_index < res->select.column_count; col_index++) {
                    free(res->select.rows[row_index][col_index]);
                }
                free(res->select.rows[row_index]);
            }
            free(res->select.rows);
        }

        if (res->select.columns) {
            for (col_index = 0; col_index < res->select.column_count; col_index++) {
                free(res->select.columns[col_index]);
            }
            free(res->select.columns);
        }
    }

    memset(res, 0, sizeof(*res));
}
