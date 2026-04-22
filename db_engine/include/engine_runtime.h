#ifndef ENGINE_RUNTIME_H
#define ENGINE_RUNTIME_H

#include <stddef.h>

#define ENGINE_RUNTIME_SCHEMA_DIR_MAX 256
#define ENGINE_RUNTIME_PATH_MAX 512

typedef enum {
    ENGINE_RUNTIME_OK = 0,
    ENGINE_RUNTIME_ERR_INVALID_ARG = -1,
    ENGINE_RUNTIME_ERR_STATE = -2,
    ENGINE_RUNTIME_ERR_SYSTEM = -3,
    ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED = -4
} EngineRuntimeStatus;

/* 전역 런타임이 어떤 단계까지 준비됐는지 노출한다. */
typedef struct {
    int initialized;
    int prepared;
    int lock_ready;
    char schema_dir[ENGINE_RUNTIME_SCHEMA_DIR_MAX];
} EngineRuntimeState;

const EngineRuntimeState *engine_runtime_get_state(void);
int engine_runtime_set_schema_dir(const char *schema_dir);
const char *engine_runtime_default_schema_dir(void);
int engine_runtime_build_schema_path(const char *table_name,
                                     char *buffer,
                                     size_t buffer_size);
int engine_runtime_build_data_path(const char *table_name,
                                   char *buffer,
                                   size_t buffer_size);
int engine_runtime_lock_shared(void);
int engine_runtime_lock_exclusive(void);
void engine_runtime_unlock_shared(void);
void engine_runtime_unlock_exclusive(void);

#endif /* ENGINE_RUNTIME_H */
