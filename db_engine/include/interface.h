#ifndef INTERFACE_H
#define INTERFACE_H

/*
 * 엔진 모듈 사이에서 공통으로 쓰는 타입과 함수 선언을 모아 둔 헤더다.
 * 구현은 각 src 디렉터리에 있고, 여기에는 선언과 데이터 구조만 둔다.
 */

/* 공통 반환 코드 */
#define SQL_OK   0
#define SQL_ERR -1

/* 토큰 종류 */
typedef enum {
    TOKEN_SELECT,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_VALUES,
    TOKEN_BETWEEN,   /* BETWEEN */
    TOKEN_AND,       /* AND */
    TOKEN_STAR,      /* * */
    TOKEN_COMMA,     /* , */
    TOKEN_LPAREN,    /* ( */
    TOKEN_RPAREN,    /* ) */
    TOKEN_EQ,        /* = */
    TOKEN_SEMICOLON, /* ; */
    TOKEN_IDENT,     /* 테이블명, 컬럼명 */
    TOKEN_STRING,    /* 'alice' */
    TOKEN_INTEGER,   /* 42 */
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char      value[256]; /* 토큰 원문 */
    int       line;       /* 에러 메시지용 줄 번호 */
} Token;

typedef struct {
    Token *tokens;
    int    count;
} TokenList;

/*
 * 호출자가 반환 메모리를 해제한다.
 * - input_read_file(): free()
 * - lexer_tokenize(): lexer_free()
 */
char      *input_read_file(const char *path);
TokenList *lexer_tokenize(const char *sql);
void       lexer_free(TokenList *list);

/* AST 문장 종류 */
typedef enum {
    STMT_SELECT,
    STMT_INSERT
} StmtType;

/* WHERE 절 형태 */
typedef enum {
    WHERE_EQ,      /* col = val */
    WHERE_BETWEEN  /* col BETWEEN val_from AND val_to */
} WhereType;

typedef struct {
    char      col[64];       /* WHERE 대상 컬럼 */
    WhereType type;          /* 조건 종류 */
    char      val[256];      /* WHERE_EQ 값 */
    char      val_from[256]; /* WHERE_BETWEEN 시작값 */
    char      val_to[256];   /* WHERE_BETWEEN 끝값 */
} WhereClause;

typedef struct {
    int          select_all;   /* SELECT * 이면 1 */
    char       **columns;      /* 선택한 컬럼 목록 */
    int          column_count;
    char         table[64];
    int          has_where;
    WhereClause  where;
} SelectStmt;

typedef struct {
    char   table[64];
    char **columns;      /* INSERT 대상 컬럼 목록, NULL이면 VALUES-only */
    int    column_count;
    char **values;       /* VALUES에 들어온 문자열 목록 */
    int    value_count;
} InsertStmt;

typedef struct {
    StmtType type;
    union {
        SelectStmt select;
        InsertStmt insert;
    };
} ASTNode;

/* 호출자가 반환 메모리를 해제한다. parser_free() 사용 */
ASTNode *parser_parse(TokenList *tokens);
void     parser_free(ASTNode *node);

/* 스키마 컬럼 타입 */
typedef enum {
    COL_INT,
    COL_VARCHAR,
    COL_BOOLEAN /* "T" / "F" */
} ColType;

typedef struct {
    char    name[64];
    ColType type;
    int     max_len; /* VARCHAR일 때만 사용, INT면 0 */
    int     is_primary_key;
    int     is_unique;
    int     is_auto_increment;
} ColDef;

typedef struct {
    char    table_name[64];
    ColDef *columns;
    int     column_count;
} TableSchema;

/* 호출자가 반환 메모리를 해제한다. schema_free() 사용 */
TableSchema *schema_load(const char *table_name);
int          schema_validate(const ASTNode *node,
                             const TableSchema *schema);
void         schema_free(TableSchema *schema);

/* 실행 결과 한 행 */
typedef struct {
    char **values;
    int    count;
} Row;

/* SELECT 결과 전체 */
typedef struct {
    char **col_names;
    int    col_count;
    Row   *rows;
    int    row_count;
} ResultSet;

/* 호출자가 반환 메모리를 해제한다. result_free() 사용 */
int        executor_run(const ASTNode *node, const TableSchema *schema);
ResultSet *db_select(const SelectStmt *stmt, const TableSchema *schema);
int        db_insert(const InsertStmt *stmt, const TableSchema *schema);
void       result_free(ResultSet *rs);

#endif /* INTERFACE_H */
