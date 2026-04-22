#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "../include/engine_api.h"

int main(void) {
    const EngineRuntimeState *state;
    EngineResponse response = {0};
    EngineErrorCode error_code = ENGINE_ERR_RUNTIME;
    char *error_message = NULL;

    assert(engine_runtime_init() == ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED);

    state = engine_runtime_get_state();
    assert(strcmp(state->schema_dir, "db_engine/schema") == 0);
    assert(engine_runtime_set_schema_dir("db_engine/schema") ==
           ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED);
    assert(engine_runtime_prepare_all() == ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED);

    assert(engine_execute_sql("SELECT * FROM users;",
                              &response,
                              &error_code,
                              &error_message) == ENGINE_API_ERR);
    assert(error_code == ENGINE_ERR_NOT_IMPLEMENTED);
    assert(error_message != NULL);
    assert(strcmp(engine_error_code_name(error_code),
                  "ENGINE_ERR_NOT_IMPLEMENTED") == 0);

    free(error_message);
    engine_response_free(&response);
    engine_runtime_shutdown();

    state = engine_runtime_get_state();
    assert(state->initialized == 0);
    assert(state->prepared == 0);

    return 0;
}
