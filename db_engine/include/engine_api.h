#ifndef ENGINE_API_H
#define ENGINE_API_H

#include "engine_runtime.h"
#include "engine_types.h"

#define ENGINE_API_OK 0
#define ENGINE_API_ERR -1

int engine_runtime_init(void);
int engine_runtime_prepare_all(void);
/*
 * err_message는 성공 시 NULL, 실패 시 free() 가능한 문자열을 돌려준다.
 * 호출자가 이전 값을 재사용 중이라면 먼저 직접 정리한다.
 */
int engine_execute_sql(const char *sql,
                       EngineResponse *out,
                       EngineErrorCode *err_code,
                       char **err_message);
void engine_response_free(EngineResponse *res);
void engine_runtime_shutdown(void);
const char *engine_error_code_name(EngineErrorCode code);

#endif /* ENGINE_API_H */
