#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  define MKDIR(p) mkdir(p, 0755)
#endif

#include "../../include/engine_runtime.h"
#include "../../include/index_manager.h"
#include "../../include/interface.h"

static char *dup_string(const char *src) {
    char *copy;
    size_t len;

    if (!src) src = "";

    len = strlen(src) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;

    memcpy(copy, src, len);
    return copy;
}

static int find_column_index(const TableSchema *schema, const char *name) {
    int i;

    if (!schema || !name) return -1;

    for (i = 0; i < schema->column_count; i++) {
        if (strcmp(schema->columns[i].name, name) == 0) return i;
    }

    return -1;
}

static int find_auto_increment_index(const TableSchema *schema) {
    int i;

    if (!schema) return -1;

    for (i = 0; i < schema->column_count; i++) {
        if (schema->columns[i].is_auto_increment) return i;
    }

    return -1;
}

static void free_row_values(char **values, int count) {
    int i;

    if (!values) return;

    for (i = 0; i < count; i++) {
        free(values[i]);
    }

    free(values);
}

static int ensure_data_directory(const char *data_path) {
    char dir_path[ENGINE_RUNTIME_PATH_MAX];
    char *last_separator;

    if (!data_path) return 0;
    if (strlen(data_path) >= sizeof(dir_path)) return 0;

    memcpy(dir_path, data_path, strlen(data_path) + 1);
    last_separator = strrchr(dir_path, '/');
    if (!last_separator) {
        last_separator = strrchr(dir_path, '\\');
    }

    if (!last_separator) {
        errno = 0;
        if (MKDIR("data") == 0 || errno == EEXIST) {
            return 1;
        }
        return 0;
    }

    *last_separator = '\0';
    if (dir_path[0] == '\0') return 1;

    errno = 0;
    if (MKDIR(dir_path) == 0 || errno == EEXIST) {
        return 1;
    }

    return 0;
}

static int read_next_column_value(const char **cursor,
                                  char *buf,
                                  size_t buf_size) {
    const char *start;
    const char *delimiter;
    const char *end;
    size_t len;

    if (!cursor || !*cursor || !buf || buf_size == 0) return 0;

    start = *cursor;
    if (*start == '\0') return 0;

    while (*start == ' ') start++;

    delimiter = start;
    while (*delimiter != '\0' &&
           *delimiter != '|' &&
           *delimiter != '\n' &&
           *delimiter != '\r') {
        delimiter++;
    }

    end = delimiter;
    while (end > start && end[-1] == ' ') {
        end--;
    }

    len = (size_t)(end - start);
    if (len >= buf_size) len = buf_size - 1;

    memcpy(buf, start, len);
    buf[len] = '\0';

    *cursor = delimiter;
    if (**cursor == '|') {
        (*cursor)++;
    }

    return 1;
}

static int line_column_value(const char *line,
                             int col_idx,
                             char *buf,
                             size_t buf_size) {
    const char *cursor = line;
    int i;

    if (!line || !buf || buf_size == 0 || col_idx < 0) return 0;

    for (i = 0; i <= col_idx; i++) {
        if (!read_next_column_value(&cursor, buf, buf_size)) {
            return 0;
        }
    }

    return 1;
}

static ResultSet *make_empty_rs(const TableSchema *schema) {
    ResultSet *rs;
    int i;

    rs = (ResultSet *)calloc(1, sizeof(ResultSet));
    if (!rs) return NULL;

    rs->col_count = schema->column_count;
    rs->col_names = (char **)calloc((size_t)rs->col_count, sizeof(char *));
    if (!rs->col_names) {
        free(rs);
        return NULL;
    }

    for (i = 0; i < rs->col_count; i++) {
        rs->col_names[i] = dup_string(schema->columns[i].name);
    }

    return rs;
}

static int line_matches_filter(const char *line,
                               const SelectStmt *stmt,
                               const TableSchema *schema) {
    int col_idx;
    char value[256];

    if (!stmt->has_where) return 1;

    col_idx = find_column_index(schema, stmt->where.col);
    if (col_idx < 0) return 1;

    if (!line_column_value(line, col_idx, value, sizeof(value))) return 0;

    if (stmt->where.type == WHERE_EQ) {
        return strcmp(value, stmt->where.val) == 0;
    }

    if (stmt->where.type == WHERE_BETWEEN) {
        int current = atoi(value);
        int from = atoi(stmt->where.val_from);
        int to = atoi(stmt->where.val_to);
        return current >= from && current <= to;
    }

    return 1;
}

static Row parse_line_to_row(const char *line, const TableSchema *schema) {
    Row row = {0};
    const char *cursor = line;
    char value[1024];
    int i;

    row.count = schema->column_count;
    row.values = (char **)calloc((size_t)row.count, sizeof(char *));
    if (!row.values) return row;
    for (i = 0; i < row.count; i++) {
        if (read_next_column_value(&cursor, value, sizeof(value))) {
            row.values[i] = dup_string(value);
        } else {
            row.values[i] = dup_string("");
        }

        if (!row.values[i]) {
            free_row_values(row.values, i);
            row.values = NULL;
            row.count = 0;
            return row;
        }
    }

    return row;
}

static int read_rows(FILE *fp,
                     const SelectStmt *stmt,
                     const TableSchema *schema,
                     Row **rows_out) {
    Row *rows;
    int capacity = 16;
    int row_count = 0;
    char line[1024];

    *rows_out = NULL;

    rows = (Row *)calloc((size_t)capacity, sizeof(Row));
    if (!rows) return -1;

    while (fgets(line, sizeof(line), fp)) {
        int len = (int)strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        if (!line_matches_filter(line, stmt, schema)) continue;

        if (row_count == capacity) {
            Row *tmp;

            capacity *= 2;
            tmp = (Row *)realloc(rows, (size_t)capacity * sizeof(Row));
            if (!tmp) break;
            rows = tmp;
        }

        rows[row_count] = parse_line_to_row(line, schema);
        if (!rows[row_count].values) break;
        row_count++;
    }

    *rows_out = rows;
    return row_count;
}

static ResultSet *build_resultset(Row *rows,
                                  int row_count,
                                  const SelectStmt *stmt,
                                  const TableSchema *schema) {
    ResultSet *rs;
    int i;

    rs = (ResultSet *)calloc(1, sizeof(ResultSet));
    if (!rs) {
        for (i = 0; i < row_count; i++) {
            int j;
            for (j = 0; j < rows[i].count; j++) free(rows[i].values[j]);
            free(rows[i].values);
        }
        free(rows);
        return NULL;
    }

    if (stmt->select_all) {
        rs->col_count = schema->column_count;
        rs->col_names = (char **)calloc((size_t)rs->col_count, sizeof(char *));
        if (!rs->col_names) {
            free(rs);
            return NULL;
        }

        for (i = 0; i < rs->col_count; i++) {
            rs->col_names[i] = dup_string(schema->columns[i].name);
        }

        rs->rows = rows;
        rs->row_count = row_count;
        return rs;
    }

    rs->col_count = stmt->column_count;
    rs->col_names = (char **)calloc((size_t)rs->col_count, sizeof(char *));
    if (!rs->col_names) {
        free(rs);
        return NULL;
    }

    rs->rows = (Row *)calloc((size_t)row_count, sizeof(Row));
    if (!rs->rows) {
        free(rs->col_names);
        free(rs);
        return NULL;
    }

    rs->row_count = row_count;
    for (i = 0; i < rs->col_count; i++) {
        rs->col_names[i] = dup_string(stmt->columns[i]);
    }

    for (i = 0; i < row_count; i++) {
        int c;

        rs->rows[i].count = rs->col_count;
        rs->rows[i].values = (char **)calloc((size_t)rs->col_count, sizeof(char *));
        if (!rs->rows[i].values) continue;

        for (c = 0; c < rs->col_count; c++) {
            int schema_idx = find_column_index(schema, stmt->columns[c]);
            rs->rows[i].values[c] = dup_string(
                (schema_idx >= 0 && schema_idx < rows[i].count)
                    ? rows[i].values[schema_idx]
                    : "");
        }
    }

    for (i = 0; i < row_count; i++) {
        int j;
        for (j = 0; j < rows[i].count; j++) free(rows[i].values[j]);
        free(rows[i].values);
    }
    free(rows);

    return rs;
}

static ResultSet *fetch_by_offset(long offset,
                                  const SelectStmt *stmt,
                                  const TableSchema *schema) {
    char path[ENGINE_RUNTIME_PATH_MAX];
    FILE *fp;
    char line[1024];
    int len;
    Row *rows;

    if (offset < 0) return make_empty_rs(schema);

    if (!engine_runtime_build_data_path(stmt->table, path, sizeof(path))) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) return make_empty_rs(schema);

    if (fseek(fp, offset, SEEK_SET) != 0 || !fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return make_empty_rs(schema);
    }

    fclose(fp);

    len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    if (len == 0) return make_empty_rs(schema);

    rows = (Row *)calloc(1, sizeof(Row));
    if (!rows) return NULL;

    rows[0] = parse_line_to_row(line, schema);
    if (!rows[0].values) {
        free(rows);
        return NULL;
    }

    return build_resultset(rows, 1, stmt, schema);
}

static ResultSet *fetch_by_offsets(const long *offsets,
                                   int count,
                                   const SelectStmt *stmt,
                                   const TableSchema *schema) {
    char path[ENGINE_RUNTIME_PATH_MAX];
    FILE *fp;
    Row *rows;
    int actual = 0;
    int i;

    if (count <= 0 || !offsets) return make_empty_rs(schema);

    if (!engine_runtime_build_data_path(stmt->table, path, sizeof(path))) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) return make_empty_rs(schema);

    rows = (Row *)calloc((size_t)count, sizeof(Row));
    if (!rows) {
        fclose(fp);
        return NULL;
    }

    for (i = 0; i < count; i++) {
        char line[1024];
        int len;

        if (fseek(fp, offsets[i], SEEK_SET) != 0) continue;
        if (!fgets(line, sizeof(line), fp)) continue;

        len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        rows[actual] = parse_line_to_row(line, schema);
        if (rows[actual].values) actual++;
    }

    fclose(fp);
    return build_resultset(rows, actual, stmt, schema);
}

static ResultSet *linear_scan(const SelectStmt *stmt, const TableSchema *schema) {
    char path[ENGINE_RUNTIME_PATH_MAX];
    FILE *fp;
    Row *rows = NULL;
    int row_count;

    if (!engine_runtime_build_data_path(stmt->table, path, sizeof(path))) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) return make_empty_rs(schema);

    row_count = read_rows(fp, stmt, schema, &rows);
    fclose(fp);

    if (row_count < 0) return NULL;
    return build_resultset(rows, row_count, stmt, schema);
}

static ResultSet *select_impl(const SelectStmt *stmt, const TableSchema *schema) {
    ResultSet *rs = NULL;

    if (!stmt || !schema) return NULL;

    if (stmt->has_where && strcmp(stmt->where.col, "id") == 0) {
        if (stmt->where.type == WHERE_EQ) {
            long offset = index_search_id(stmt->table, atoi(stmt->where.val));
            rs = fetch_by_offset(offset, stmt, schema);
        } else if (stmt->where.type == WHERE_BETWEEN) {
            int count = 0;
            long *offsets = index_range_id_alloc(
                stmt->table,
                atoi(stmt->where.val_from),
                atoi(stmt->where.val_to),
                &count);
            rs = fetch_by_offsets(offsets, count, stmt, schema);
            free(offsets);
        }
    } else if (stmt->has_where &&
               strcmp(stmt->where.col, "age") == 0 &&
               stmt->where.type == WHERE_BETWEEN) {
        int count = 0;
        long *offsets = index_range_age_alloc(
            stmt->table,
            atoi(stmt->where.val_from),
            atoi(stmt->where.val_to),
            &count);
        rs = fetch_by_offsets(offsets, count, stmt, schema);
        free(offsets);
    }

    if (!rs) rs = linear_scan(stmt, schema);
    return rs;
}

static int build_insert_row_values(const InsertStmt *stmt,
                                   const TableSchema *schema,
                                   char ***out_values,
                                   int *out_generated_id) {
    char **row_values;
    int auto_col;
    int i;
    int generated_id = 0;

    if (!stmt || !schema || !out_values) return SQL_ERR;

    row_values = (char **)calloc((size_t)schema->column_count, sizeof(char *));
    if (!row_values) return SQL_ERR;

    auto_col = find_auto_increment_index(schema);

    if (stmt->column_count == 0) {
        if (auto_col >= 0) {
            fprintf(stderr,
                    "executor: AUTO_INCREMENT table '%s' requires a column "
                    "list insert\n",
                    stmt->table);
            free(row_values);
            return SQL_ERR;
        }

        if (stmt->value_count != schema->column_count) {
            free(row_values);
            return SQL_ERR;
        }

        for (i = 0; i < schema->column_count; i++) {
            row_values[i] = dup_string(stmt->values[i]);
            if (!row_values[i]) {
                free_row_values(row_values, schema->column_count);
                return SQL_ERR;
            }
        }

        *out_values = row_values;
        return SQL_OK;
    }

    if (auto_col >= 0) {
        int next_id = 0;
        char id_buf[32];

        if (index_next_auto_id(stmt->table, &next_id) != 0) {
            fprintf(stderr,
                    "executor: failed to allocate AUTO_INCREMENT id for '%s'\n",
                    stmt->table);
            free(row_values);
            return SQL_ERR;
        }

        snprintf(id_buf, sizeof(id_buf), "%d", next_id);
        generated_id = next_id;
        row_values[auto_col] = dup_string(id_buf);
        if (!row_values[auto_col]) {
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }
    }

    for (i = 0; i < stmt->column_count; i++) {
        int schema_idx = find_column_index(schema, stmt->columns[i]);

        if (schema_idx < 0) {
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }

        if (auto_col >= 0 && schema_idx == auto_col) {
            fprintf(stderr,
                    "executor: AUTO_INCREMENT column '%s' cannot be "
                    "inserted explicitly\n",
                    schema->columns[schema_idx].name);
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }

        if (row_values[schema_idx]) {
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }

        row_values[schema_idx] = dup_string(stmt->values[i]);
        if (!row_values[schema_idx]) {
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }
    }

    for (i = 0; i < schema->column_count; i++) {
        if (!row_values[i]) {
            fprintf(stderr,
                    "executor: missing value for column '%s'\n",
                    schema->columns[i].name);
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }
    }

    *out_values = row_values;
    if (out_generated_id) *out_generated_id = generated_id;
    return SQL_OK;
}

ResultSet *db_select(const SelectStmt *stmt, const TableSchema *schema) {
    return select_impl(stmt, schema);
}

static int insert_impl(const InsertStmt *stmt,
                       const TableSchema *schema,
                       int *out_generated_id) {
    char **row_values = NULL;
    char path[ENGINE_RUNTIME_PATH_MAX];
    FILE *fp;
    long offset;
    int id_col;
    int age_col;
    int generated_id = 0;

    if (out_generated_id) *out_generated_id = 0;
    if (!stmt || !schema) return SQL_ERR;

    if (build_insert_row_values(stmt, schema, &row_values, &generated_id) != SQL_OK) {
        return SQL_ERR;
    }

    if (!engine_runtime_build_data_path(stmt->table, path, sizeof(path))) {
        free_row_values(row_values, schema->column_count);
        return SQL_ERR;
    }

    if (!ensure_data_directory(path)) {
        free_row_values(row_values, schema->column_count);
        return SQL_ERR;
    }

    fp = fopen(path, "ab");
    if (!fp) {
        fprintf(stderr, "executor: cannot open '%s'\n", path);
        free_row_values(row_values, schema->column_count);
        return SQL_ERR;
    }

    offset = ftell(fp);

    for (int i = 0; i < schema->column_count; i++) {
        fprintf(fp, "%s", row_values[i]);
        if (i < schema->column_count - 1) fprintf(fp, " | ");
    }
    fprintf(fp, "\n");
    fclose(fp);

    id_col = find_column_index(schema, "id");
    if (id_col >= 0) {
        if (index_insert_id(stmt->table, atoi(row_values[id_col]), offset) != 0) {
            fprintf(stderr,
                    "executor: failed to update id index for '%s'\n",
                    stmt->table);
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }
    }

    age_col = find_column_index(schema, "age");
    if (age_col >= 0) {
        if (index_insert_age(stmt->table, atoi(row_values[age_col]), offset) != 0) {
            fprintf(stderr,
                    "executor: failed to update age index for '%s'\n",
                    stmt->table);
            free_row_values(row_values, schema->column_count);
            return SQL_ERR;
        }
    }

    if (out_generated_id) *out_generated_id = generated_id;
    free_row_values(row_values, schema->column_count);
    return SQL_OK;
}

int db_insert(const InsertStmt *stmt, const TableSchema *schema) {
    return insert_impl(stmt, schema, NULL);
}

int db_insert_with_generated_id(const InsertStmt *stmt,
                                const TableSchema *schema,
                                int *out_generated_id) {
    return insert_impl(stmt, schema, out_generated_id);
}

int executor_run(const ASTNode *node, const TableSchema *schema) {
    if (!node || !schema) return SQL_ERR;

    switch (node->type) {
        case STMT_INSERT:
            if (db_insert(&node->insert, schema) != SQL_OK) return SQL_ERR;
            printf("1 row inserted.\n");
            return SQL_OK;

        case STMT_SELECT: {
            ResultSet *rs = db_select(&node->select, schema);
            if (!rs) return SQL_ERR;
            result_free(rs);
            return SQL_OK;
        }

        default:
            fprintf(stderr, "executor: unknown statement type\n");
            return SQL_ERR;
    }
}

void result_free(ResultSet *rs) {
    int i;

    if (!rs) return;

    for (i = 0; i < rs->row_count; i++) {
        int j;
        for (j = 0; j < rs->rows[i].count; j++) free(rs->rows[i].values[j]);
        free(rs->rows[i].values);
    }

    free(rs->rows);

    for (i = 0; i < rs->col_count; i++) free(rs->col_names[i]);
    free(rs->col_names);

    free(rs);
}
