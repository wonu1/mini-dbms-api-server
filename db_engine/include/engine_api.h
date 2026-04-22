#ifndef ENGINE_API_H
#define ENGINE_API_H

#include "engine_runtime.h"
#include "engine_types.h"

#define ENGINE_API_OK 0
#define ENGINE_API_ERR -1

/*
 * API 서버가 DB 엔진을 직접 만지지 않고 호출하는 공개 진입점들이다.
 * server 쪽은 lexer/parser/schema/executor 세부 구조를 몰라도
 * engine_execute_sql() 하나로 SQL 실행을 요청할 수 있다.
 */

/* 엔진 런타임 전역 상태와 lock을 준비한다. */
int engine_runtime_init(void);

/* 서버 시작 전에 필요한 schema/index 준비 작업을 수행한다. */
int engine_runtime_prepare_all(void);

/*
 * err_message는 성공 시 NULL, 실패 시 free() 가능한 문자열을 돌려준다.
 * 호출자가 이전 값을 재사용 중이라면 먼저 직접 정리한다.
 */
int engine_execute_sql(const char *sql,
                       EngineResponse *out,
                       EngineErrorCode *err_code,
                       char **err_message);

/* EngineResponse 안에 들어 있는 동적 메모리를 정리한다. */
void engine_response_free(EngineResponse *res);

/* 엔진 런타임 전역 상태와 lock을 정리한다. */
void engine_runtime_shutdown(void);

/* EngineErrorCode를 디버깅하기 쉬운 문자열로 바꾼다. */
const char *engine_error_code_name(EngineErrorCode code);

#endif /* ENGINE_API_H */
