#include <string.h>

#include "../../include/engine_api.h"

static EngineRuntimeState g_state;

const EngineRuntimeState *engine_runtime_get_state(void) {
    return &g_state;
}

const char *engine_runtime_default_schema_dir(void) {
    return "db_engine/schema";
}

int engine_runtime_set_schema_dir(const char *schema_dir) {
    if (!schema_dir || schema_dir[0] == '\0') {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    (void)schema_dir;

    /* TODO: Store and validate the schema directory path. */
    return ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED;
}

int engine_runtime_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    strncpy(g_state.schema_dir,
            engine_runtime_default_schema_dir(),
            sizeof(g_state.schema_dir) - 1);

    /* TODO: Initialize global runtime state and the RW lock. */
    return ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED;
}

int engine_runtime_prepare_all(void) {
    /* TODO: Scan schemas and pre-initialize table runtime state. */
    return ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED;
}

void engine_runtime_shutdown(void) {
    memset(&g_state, 0, sizeof(g_state));
}
