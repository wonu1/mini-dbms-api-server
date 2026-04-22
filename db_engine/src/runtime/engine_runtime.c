#include <string.h>

#include "../../include/engine_api.h"
#include "../../include/index_manager.h"

static EngineRuntimeState g_state;

const EngineRuntimeState *engine_runtime_get_state(void) {
    return &g_state;
}

const char *engine_runtime_default_schema_dir(void) {
    return "db_engine/schema";
}

int engine_runtime_set_schema_dir(const char *schema_dir) {
    size_t len;

    if (!schema_dir || schema_dir[0] == '\0') {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    len = strlen(schema_dir);
    if (len >= sizeof(g_state.schema_dir)) {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    memcpy(g_state.schema_dir, schema_dir, len + 1);
    return ENGINE_RUNTIME_OK;
}

int engine_runtime_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    strncpy(g_state.schema_dir,
            engine_runtime_default_schema_dir(),
            sizeof(g_state.schema_dir) - 1);
    g_state.initialized = 1;
    return ENGINE_RUNTIME_OK;
}

int engine_runtime_prepare_all(void) {
    /* TODO: Scan schemas and pre-initialize table runtime state. */
    return ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED;
}

void engine_runtime_shutdown(void) {
    index_cleanup();
    memset(&g_state, 0, sizeof(g_state));
}
