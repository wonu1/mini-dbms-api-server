#include <stdlib.h>
#include <string.h>

#include "../../include/engine_api.h"
#include "../../include/interface.h"
#include "../../include/index_manager.h"

#ifdef _WIN32
#  include <direct.h>
#  define ENGINE_GETCWD _getcwd
#  define ENGINE_CHDIR _chdir
#else
#  include <unistd.h>
#  define ENGINE_GETCWD getcwd
#  define ENGINE_CHDIR chdir
#endif

#define ENGINE_CWD_MAX 4096

static char *dup_string(const char *src) {
    size_t len;
    char *copy;

    if (!src) return NULL;

    len = strlen(src) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;

    memcpy(copy, src, len);
    return copy;
}

static void set_error(EngineErrorCode *err_code,
                      char **err_message,
                      EngineErrorCode code,
                      const char *message) {
    if (err_code) *err_code = code;
    if (err_message) *err_message = dup_string(message);
}

static void free_copied_select(EngineResponse *out) {
    engine_response_free(out);
}

static int copy_select_response(const ResultSet *source, EngineResponse *out) {
    int row_index;
    int col_index;

    out->type = ENGINE_RESULT_SELECT;
    out->select.column_count = source->col_count;
    out->select.row_count = source->row_count;

    out->select.columns = (char **)calloc((size_t)source->col_count, sizeof(char *));
    if (!out->select.columns) return ENGINE_API_ERR;

    for (col_index = 0; col_index < source->col_count; col_index++) {
        out->select.columns[col_index] = dup_string(source->col_names[col_index]);
        if (!out->select.columns[col_index]) {
            free_copied_select(out);
            return ENGINE_API_ERR;
        }
    }

    out->select.rows = (char ***)calloc((size_t)source->row_count, sizeof(char **));
    if (source->row_count > 0 && !out->select.rows) {
        free_copied_select(out);
        return ENGINE_API_ERR;
    }

    for (row_index = 0; row_index < source->row_count; row_index++) {
        out->select.rows[row_index] =
            (char **)calloc((size_t)source->col_count, sizeof(char *));
        if (!out->select.rows[row_index]) {
            free_copied_select(out);
            return ENGINE_API_ERR;
        }

        for (col_index = 0; col_index < source->col_count; col_index++) {
            out->select.rows[row_index][col_index] =
                dup_string(source->rows[row_index].values[col_index]);
            if (!out->select.rows[row_index][col_index]) {
                free_copied_select(out);
                return ENGINE_API_ERR;
            }
        }
    }

    return ENGINE_API_OK;
}

static int derive_workdir(char *buffer, size_t buffer_size) {
    const EngineRuntimeState *state = engine_runtime_get_state();
    const char *schema_dir = state->schema_dir[0]
        ? state->schema_dir
        : engine_runtime_default_schema_dir();
    size_t len;
    size_t index;
    int separator_index = -1;

    if (!buffer || buffer_size == 0) return 0;

    len = strlen(schema_dir);
    if (len == 0 || len >= buffer_size) return 0;

    memcpy(buffer, schema_dir, len + 1);

    while (len > 0 &&
           (buffer[len - 1] == '/' || buffer[len - 1] == '\\')) {
        buffer[--len] = '\0';
    }

    for (index = 0; index < len; index++) {
        if (buffer[index] == '/' || buffer[index] == '\\') {
            separator_index = (int)index;
        }
    }

    if (separator_index < 0) {
        buffer[0] = '.';
        buffer[1] = '\0';
        return 1;
    }

    if (separator_index == 0) {
        buffer[1] = '\0';
        return 1;
    }

    buffer[separator_index] = '\0';
    return 1;
}

static TokenList *build_single_statement_tokens(const TokenList *all_tokens) {
    TokenList *statement_tokens;
    int end = 0;
    int count;
    int index;

    if (!all_tokens || all_tokens->count == 0) return NULL;

    while (end < all_tokens->count &&
           all_tokens->tokens[end].type != TOKEN_SEMICOLON &&
           all_tokens->tokens[end].type != TOKEN_EOF) {
        end++;
    }

    count = end;
    if (count == 0) return NULL;

    if (end < all_tokens->count &&
        all_tokens->tokens[end].type == TOKEN_SEMICOLON) {
        if (end + 1 >= all_tokens->count ||
            all_tokens->tokens[end + 1].type != TOKEN_EOF) {
            return NULL;
        }
    } else if (end >= all_tokens->count ||
               all_tokens->tokens[end].type != TOKEN_EOF) {
        return NULL;
    }

    statement_tokens = (TokenList *)calloc(1, sizeof(TokenList));
    if (!statement_tokens) return NULL;

    statement_tokens->count = count + 1;
    statement_tokens->tokens =
        (Token *)calloc((size_t)statement_tokens->count, sizeof(Token));
    if (!statement_tokens->tokens) {
        free(statement_tokens);
        return NULL;
    }

    for (index = 0; index < count; index++) {
        statement_tokens->tokens[index] = all_tokens->tokens[index];
    }

    statement_tokens->tokens[count].type = TOKEN_EOF;
    statement_tokens->tokens[count].value[0] = '\0';
    statement_tokens->tokens[count].line =
        all_tokens->tokens[count > 0 ? count - 1 : 0].line;

    return statement_tokens;
}

const char *engine_error_code_name(EngineErrorCode code) {
    switch (code) {
        case ENGINE_ERR_PARSE:
            return "ENGINE_ERR_PARSE";
        case ENGINE_ERR_VALIDATION:
            return "ENGINE_ERR_VALIDATION";
        case ENGINE_ERR_UNSUPPORTED:
            return "ENGINE_ERR_UNSUPPORTED";
        case ENGINE_ERR_RUNTIME:
            return "ENGINE_ERR_RUNTIME";
        case ENGINE_ERR_NOT_IMPLEMENTED:
        default:
            return "ENGINE_ERR_NOT_IMPLEMENTED";
    }
}

int engine_execute_sql(const char *sql,
                       EngineResponse *out,
                       EngineErrorCode *err_code,
                       char **err_message) {
    TokenList *tokens = NULL;
    TokenList *statement_tokens = NULL;
    ASTNode *node = NULL;
    TableSchema *schema = NULL;
    ResultSet *result = NULL;
    char workdir[ENGINE_CWD_MAX];
    char original_cwd[ENGINE_CWD_MAX];
    int entered_workdir = 0;
    int generated_id = 0;
    int status = ENGINE_API_ERR;
    const char *table_name;

    if (!sql || !out || !err_code || !err_message) {
        return ENGINE_API_ERR;
    }

    memset(out, 0, sizeof(*out));
    out->type = ENGINE_RESULT_ERROR;
    *err_message = NULL;

    tokens = lexer_tokenize(sql);
    if (!tokens) {
        set_error(err_code, err_message,
                  ENGINE_ERR_PARSE,
                  "failed to tokenize SQL input");
        goto cleanup;
    }

    statement_tokens = build_single_statement_tokens(tokens);
    if (!statement_tokens) {
        set_error(err_code, err_message,
                  ENGINE_ERR_PARSE,
                  "engine_execute_sql accepts exactly one non-empty SQL statement");
        goto cleanup;
    }

    node = parser_parse(statement_tokens);
    if (!node) {
        set_error(err_code, err_message,
                  ENGINE_ERR_PARSE,
                  "failed to parse SQL statement");
        goto cleanup;
    }

    switch (node->type) {
        case STMT_SELECT:
            table_name = node->select.table;
            break;
        case STMT_INSERT:
            table_name = node->insert.table;
            break;
        default:
            set_error(err_code, err_message,
                      ENGINE_ERR_UNSUPPORTED,
                      "unsupported SQL statement type");
            goto cleanup;
    }

    if (!derive_workdir(workdir, sizeof(workdir))) {
        set_error(err_code, err_message,
                  ENGINE_ERR_RUNTIME,
                  "failed to derive engine working directory");
        goto cleanup;
    }

    if (!ENGINE_GETCWD(original_cwd, sizeof(original_cwd))) {
        set_error(err_code, err_message,
                  ENGINE_ERR_RUNTIME,
                  "failed to capture current working directory");
        goto cleanup;
    }

    if (ENGINE_CHDIR(workdir) != 0) {
        set_error(err_code, err_message,
                  ENGINE_ERR_RUNTIME,
                  "failed to enter engine working directory");
        goto cleanup;
    }
    entered_workdir = 1;

    schema = schema_load(table_name);
    if (!schema) {
        set_error(err_code, err_message,
                  ENGINE_ERR_VALIDATION,
                  "schema validation failed or table schema was not found");
        goto cleanup;
    }

    if (schema_validate(node, schema) != SQL_OK) {
        set_error(err_code, err_message,
                  ENGINE_ERR_VALIDATION,
                  "schema validation failed");
        goto cleanup;
    }

    if (index_init(table_name, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) != 0) {
        set_error(err_code, err_message,
                  ENGINE_ERR_RUNTIME,
                  "failed to initialize engine indexes");
        goto cleanup;
    }

    if (node->type == STMT_SELECT) {
        result = db_select(&node->select, schema);
        if (!result) {
            set_error(err_code, err_message,
                      ENGINE_ERR_RUNTIME,
                      "select execution failed");
            goto cleanup;
        }

        if (copy_select_response(result, out) != ENGINE_API_OK) {
            set_error(err_code, err_message,
                      ENGINE_ERR_RUNTIME,
                      "failed to allocate SELECT response");
            goto cleanup;
        }
    } else {
        if (db_insert_with_generated_id(&node->insert, schema, &generated_id) != SQL_OK) {
            set_error(err_code, err_message,
                      ENGINE_ERR_RUNTIME,
                      "insert execution failed");
            goto cleanup;
        }

        out->type = ENGINE_RESULT_INSERT;
        out->insert.affected_rows = 1;
        out->insert.has_generated_id = generated_id > 0 ? 1 : 0;
        out->insert.generated_id = generated_id;
    }

    status = ENGINE_API_OK;

cleanup:
    if (entered_workdir && ENGINE_CHDIR(original_cwd) != 0) {
        if (status == ENGINE_API_OK) {
            engine_response_free(out);
        }
        if (*err_message == NULL) {
            set_error(err_code, err_message,
                      ENGINE_ERR_RUNTIME,
                      "failed to restore original working directory");
        } else {
            *err_code = ENGINE_ERR_RUNTIME;
        }
        status = ENGINE_API_ERR;
    }

    result_free(result);
    schema_free(schema);
    parser_free(node);
    lexer_free(statement_tokens);
    lexer_free(tokens);
    return status;
}

void engine_response_free(EngineResponse *res) {
    int row_index;
    int col_index;

    if (!res) return;

    if (res->type == ENGINE_RESULT_SELECT) {
        if (res->select.rows) {
            for (row_index = 0; row_index < res->select.row_count; row_index++) {
                if (!res->select.rows[row_index]) continue;
                for (col_index = 0; col_index < res->select.column_count; col_index++) {
                    free(res->select.rows[row_index][col_index]);
                }
                free(res->select.rows[row_index]);
            }
            free(res->select.rows);
        }

        if (res->select.columns) {
            for (col_index = 0; col_index < res->select.column_count; col_index++) {
                free(res->select.columns[col_index]);
            }
            free(res->select.columns);
        }
    }

    memset(res, 0, sizeof(*res));
}
