#ifndef ENGINE_RUNTIME_H
#define ENGINE_RUNTIME_H

#include <stddef.h>

#define ENGINE_RUNTIME_SCHEMA_DIR_MAX 256
#define ENGINE_RUNTIME_PATH_MAX 512

/*
 * engine runtime의 상태/실패 이유를 나타내는 코드다.
 * API 서버는 이 값을 보고 시작 실패인지, 인자 문제인지 구분할 수 있다.
 */
typedef enum {
    ENGINE_RUNTIME_OK = 0,
    ENGINE_RUNTIME_ERR_INVALID_ARG = -1,
    ENGINE_RUNTIME_ERR_STATE = -2,
    ENGINE_RUNTIME_ERR_SYSTEM = -3,
    ENGINE_RUNTIME_ERR_NOT_IMPLEMENTED = -4
} EngineRuntimeStatus;

/* 전역 런타임이 어떤 단계까지 준비됐는지 노출한다. */
typedef struct {
    /* engine_runtime_init()이 성공했는지 */
    int initialized;

    /* schema/index 준비가 끝났는지 */
    int prepared;

    /* read/write lock이 준비됐는지 */
    int lock_ready;

    /* schema 파일들이 들어 있는 디렉터리 */
    char schema_dir[ENGINE_RUNTIME_SCHEMA_DIR_MAX];
} EngineRuntimeState;

/* 현재 전역 런타임 상태를 읽기 전용 포인터로 돌려준다. */
const EngineRuntimeState *engine_runtime_get_state(void);

/* schema 디렉터리 경로를 설정한다. */
int engine_runtime_set_schema_dir(const char *schema_dir);

/* 기본 schema 디렉터리 경로를 반환한다. */
const char *engine_runtime_default_schema_dir(void);

/* table 이름으로 schema 파일 경로를 만든다. */
int engine_runtime_build_schema_path(const char *table_name,
                                     char *buffer,
                                     size_t buffer_size);

/* table 이름으로 data 파일 경로를 만든다. */
int engine_runtime_build_data_path(const char *table_name,
                                   char *buffer,
                                   size_t buffer_size);

/* SELECT처럼 읽기 작업용 shared lock을 잡는다. */
int engine_runtime_lock_shared(void);

/* INSERT처럼 쓰기 작업용 exclusive lock을 잡는다. */
int engine_runtime_lock_exclusive(void);

/* shared lock을 해제한다. */
void engine_runtime_unlock_shared(void);

/* exclusive lock을 해제한다. */
void engine_runtime_unlock_exclusive(void);

#endif /* ENGINE_RUNTIME_H */
