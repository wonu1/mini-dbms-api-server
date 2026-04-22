#ifndef ENGINE_TYPES_H
#define ENGINE_TYPES_H

/*
 * API 서버가 HTTP JSON으로 바꾸기 쉬운 엔진 응답 타입들이다.
 * 기존 DB 엔진 내부 ResultSet/AST를 그대로 노출하지 않고,
 * 서버용으로 단순한 형태만 공개한다.
 */

typedef enum {
    /* SELECT 결과 */
    ENGINE_RESULT_SELECT,

    /* INSERT 결과 */
    ENGINE_RESULT_INSERT,

    /* 에러 결과 */
    ENGINE_RESULT_ERROR
} EngineResultType;

typedef enum {
    /* SQL 문법 파싱 실패 */
    ENGINE_ERR_PARSE,

    /* schema 검증 실패 */
    ENGINE_ERR_VALIDATION,

    /* 아직 지원하지 않는 SQL */
    ENGINE_ERR_UNSUPPORTED,

    /* 파일/인덱스/락 같은 실행 중 실패 */
    ENGINE_ERR_RUNTIME,

    /* 아직 구현되지 않은 기능 */
    ENGINE_ERR_NOT_IMPLEMENTED
} EngineErrorCode;

/* 서버가 SELECT 결과를 JSON으로 바꾸기 쉬운 형태다. */
typedef struct {
    /* 컬럼 이름 배열 */
    char **columns;

    /* 컬럼 개수 */
    int column_count;

    /* rows[row][column] 형태의 2차원 문자열 배열 */
    char ***rows;

    /* 행 개수 */
    int row_count;
} EngineSelectResult;

typedef struct {
    /* INSERT로 영향받은 행 수 */
    int affected_rows;

    /* generated_id가 있는지 여부 */
    int has_generated_id;

    /* AUTO_INCREMENT 등으로 생성된 id */
    int generated_id;
} EngineInsertResult;

typedef struct {
    /* union 안에서 어떤 결과를 읽어야 하는지 알려준다. */
    EngineResultType type;

    /* type이 SELECT면 select, INSERT면 insert를 읽는다. */
    union {
        EngineSelectResult select;
        EngineInsertResult insert;
    };
} EngineResponse;

#endif /* ENGINE_TYPES_H */
