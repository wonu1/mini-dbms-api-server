#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/interface.h"
#include "../include/index_manager.h"

/*
 * DB 엔진 CLI 실행 파일의 진입점이다.
 * SQL 파일을 읽고, 세미콜론 기준으로 문장을 나눈 뒤,
 * 각 문장을 lexer -> parser -> schema validation -> executor 순서로 실행한다.
 */

/* SELECT 결과를 콘솔에서 읽기 쉽게 표 형태로 출력한다. */
static void print_pretty_table(ResultSet *rs) {
    if (!rs || rs->row_count == 0) {
        printf("(0 rows)\n");
        return;
    }

    int *widths = (int *)calloc((size_t)rs->col_count, sizeof(int));
    if (!widths) return;

    for (int c = 0; c < rs->col_count; c++) {
        widths[c] = (int)strlen(rs->col_names[c]);
        for (int r = 0; r < rs->row_count; r++) {
            int len = (int)strlen(rs->rows[r].values[c]);
            if (len > widths[c]) widths[c] = len;
        }
    }

#define PRINT_SEP() do { \
    for (int c = 0; c < rs->col_count; c++) { \
        printf("+"); \
        for (int w = 0; w < widths[c] + 2; w++) printf("-"); \
    } \
    printf("+\n"); \
} while (0)

    PRINT_SEP();
    for (int c = 0; c < rs->col_count; c++)
        printf("| %-*s ", widths[c], rs->col_names[c]);
    printf("|\n");
    PRINT_SEP();

    for (int r = 0; r < rs->row_count; r++) {
        for (int c = 0; c < rs->rows[r].count; c++)
            printf("| %-*s ", widths[c], rs->rows[r].values[c]);
        printf("|\n");
    }

    PRINT_SEP();
    printf("(%d rows)\n", rs->row_count);

    free(widths);
#undef PRINT_SEP
}

/*
 * 토큰 목록에서 세미콜론 기준으로 한 문장만 잘라낸다.
 * 여러 SQL 문장을 한 파일에서 순서대로 실행하기 위해 쓴다.
 */
static TokenList *split_tokens(const TokenList *all, int start,
                               int *next_start) {
    int end = start;

    while (end < all->count &&
           all->tokens[end].type != TOKEN_SEMICOLON &&
           all->tokens[end].type != TOKEN_EOF)
        end++;

    int token_count = end - start;
    if (token_count == 0) {
        *next_start = (end < all->count &&
                       all->tokens[end].type == TOKEN_SEMICOLON)
                      ? end + 1 : end;
        return NULL;
    }

    TokenList *sub = (TokenList *)malloc(sizeof(TokenList));
    if (!sub) return NULL;

    sub->count = token_count + 1;
    sub->tokens = (Token *)calloc((size_t)sub->count, sizeof(Token));
    if (!sub->tokens) {
        free(sub);
        return NULL;
    }

    for (int i = 0; i < token_count; i++)
        sub->tokens[i] = all->tokens[start + i];

    sub->tokens[token_count].type = TOKEN_EOF;
    sub->tokens[token_count].value[0] = '\0';
    sub->tokens[token_count].line =
        all->tokens[end > 0 ? end - 1 : 0].line;

    *next_start = (end < all->count &&
                   all->tokens[end].type == TOKEN_SEMICOLON)
                  ? end + 1 : end;
    return sub;
}

/* 파싱, 스키마 검증, 인덱스 초기화, 실행까지 한 문장 처리 흐름을 묶는다. */
static int run_statement(TokenList *tokens) {
    ASTNode *ast = parser_parse(tokens);
    if (!ast) {
        fprintf(stderr, "Error: parsing failed\n");
        return SQL_ERR;
    }

    const char *table = (ast->type == STMT_SELECT)
                        ? ast->select.table
                        : ast->insert.table;

    TableSchema *schema = schema_load(table);
    if (!schema) {
        fprintf(stderr, "Error: schema not found for table '%s'\n", table);
        parser_free(ast);
        return SQL_ERR;
    }

    if (schema_validate(ast, schema) != SQL_OK) {
        fprintf(stderr, "Error: schema validation failed\n");
        schema_free(schema);
        parser_free(ast);
        return SQL_ERR;
    }

    if (index_init(table, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) != 0) {
        fprintf(stderr, "Error: index_init failed for table '%s'\n", table);
        schema_free(schema);
        parser_free(ast);
        return SQL_ERR;
    }

    int status = SQL_OK;
    if (ast->type == STMT_SELECT) {
        ResultSet *rs = db_select(&ast->select, schema);
        if (!rs) {
            fprintf(stderr, "Error: select failed\n");
            status = SQL_ERR;
        } else {
            print_pretty_table(rs);
            result_free(rs);
        }
    } else {
        status = db_insert(&ast->insert, schema);
        if (status != SQL_OK) {
            fprintf(stderr, "Error: insert failed\n");
        } else {
            printf("1 row inserted.\n");
        }
    }

    schema_free(schema);
    parser_free(ast);
    return status;
}

static void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <sql_file>\n", argv0);
}

int main(int argc, char *argv[]) {
    if (argc != 2 || argv[1][0] == '-') {
        print_usage(argv[0]);
        return 1;
    }

    const char *sql_path = argv[1];
    char *sql = input_read_file(sql_path);
    if (!sql) {
        fprintf(stderr, "Error: cannot open '%s'\n", sql_path);
        return 1;
    }

    TokenList *all_tokens = lexer_tokenize(sql);
    free(sql);
    if (!all_tokens) {
        fprintf(stderr, "Error: tokenization failed\n");
        return 1;
    }

    int total = 0;
    int fail = 0;
    int pos = 0;

    while (pos < all_tokens->count) {
        if (all_tokens->tokens[pos].type == TOKEN_EOF) break;

        int next = 0;
        TokenList *sub = split_tokens(all_tokens, pos, &next);
        if (sub) {
            total++;
            if (run_statement(sub) != SQL_OK) fail++;
            lexer_free(sub);
        }
        pos = next;
    }

    lexer_free(all_tokens);

    if (total > 1) {
        printf("\n%d statement(s) executed", total);
        if (fail > 0) printf(", %d failed", fail);
        printf(".\n");
    }

    index_cleanup();
    return fail > 0 ? 1 : 0;
}
