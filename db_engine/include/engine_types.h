#ifndef ENGINE_TYPES_H
#define ENGINE_TYPES_H

typedef enum {
    ENGINE_RESULT_SELECT,
    ENGINE_RESULT_INSERT,
    ENGINE_RESULT_ERROR
} EngineResultType;

typedef enum {
    ENGINE_ERR_PARSE,
    ENGINE_ERR_VALIDATION,
    ENGINE_ERR_UNSUPPORTED,
    ENGINE_ERR_RUNTIME,
    ENGINE_ERR_NOT_IMPLEMENTED
} EngineErrorCode;

/* 서버가 SELECT 결과를 JSON으로 바꾸기 쉬운 형태다. */
typedef struct {
    char **columns;
    int column_count;
    char ***rows;
    int row_count;
} EngineSelectResult;

typedef struct {
    int affected_rows;
    int has_generated_id;
    int generated_id;
} EngineInsertResult;

typedef struct {
    EngineResultType type;
    union {
        EngineSelectResult select;
        EngineInsertResult insert;
    };
} EngineResponse;

#endif /* ENGINE_TYPES_H */
