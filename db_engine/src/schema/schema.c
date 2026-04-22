#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/engine_runtime.h"
#include "../../include/interface.h"

/*
 * 이 파일은 table schema 파일을 읽고, AST가 schema 규칙에 맞는지 검사한다.
 * 예를 들어 존재하지 않는 컬럼을 SELECT하거나, AUTO_INCREMENT 컬럼을 직접 INSERT하면
 * 여기서 오류로 막는다.
 */

static int is_integer_string(const char *s) {
    int i = 0;

    if (!s || *s == '\0') return 0;

    if (s[0] == '-') i = 1;
    if (s[i] == '\0') return 0;

    for (; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) return 0;
    }

    return 1;
}

static int is_boolean_string(const char *s) {
    return (s && (strcmp(s, "T") == 0 || strcmp(s, "F") == 0));
}

static char *trim_whitespace(char *s) {
    char *end;

    if (!s) return s;

    while (*s != '\0' && isspace((unsigned char)*s)) s++;

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';

    return s;
}

static int find_column_index(const TableSchema *schema, const char *col_name) {
    int i;

    if (!schema || !col_name) return -1;

    for (i = 0; i < schema->column_count; i++) {
        if (strcmp(schema->columns[i].name, col_name) == 0) return i;
    }

    return -1;
}

static int find_auto_increment_column(const TableSchema *schema) {
    int i;

    if (!schema) return -1;

    for (i = 0; i < schema->column_count; i++) {
        if (schema->columns[i].is_auto_increment) return i;
    }

    return -1;
}

static int validate_value_for_column(const ColDef *column, const char *value) {
    if (!column || !value) return SQL_ERR;

    if (column->type == COL_INT && !is_integer_string(value)) {
        fprintf(stderr,
                "schema: column '%s' expects INT, got '%s'\n",
                column->name, value);
        return SQL_ERR;
    }

    if (column->type == COL_VARCHAR) {
        if (column->max_len > 0 && (int)strlen(value) > column->max_len) {
            fprintf(stderr,
                    "schema: column '%s' max length %d, got %d\n",
                    column->name, column->max_len, (int)strlen(value));
            return SQL_ERR;
        }
    }

    if (column->type == COL_BOOLEAN && !is_boolean_string(value)) {
        fprintf(stderr,
                "schema: column '%s' expects BOOLEAN (T/F), got '%s'\n",
                column->name, value);
        return SQL_ERR;
    }

    return SQL_OK;
}

static ColType column_type(const TableSchema *schema, const char *col_name) {
    int idx = find_column_index(schema, col_name);
    if (idx < 0) return COL_INT;
    return schema->columns[idx].type;
}

static int split_schema_fields(char *value, char **fields, int max_fields) {
    char *cursor;
    int count = 0;

    if (!value || !fields || max_fields <= 0) return 0;

    cursor = value;
    while (*cursor != '\0' && count < max_fields) {
        char *field_start = cursor;

        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }

        if (*cursor == ',') {
            *cursor = '\0';
            cursor++;
        }

        fields[count++] = trim_whitespace(field_start);
    }

    return count;
}

TableSchema *schema_load(const char *table_name) {
    char path[ENGINE_RUNTIME_PATH_MAX];
    FILE *fp = NULL;
    TableSchema *schema = NULL;
    char line[512];
    int col_count = 0;

    if (!table_name) return NULL;

    if (!engine_runtime_build_schema_path(table_name, path, sizeof(path))) {
        return NULL;
    }

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "schema: cannot open '%s'\n", path);
        return NULL;
    }

    schema = (TableSchema *)calloc(1, sizeof(TableSchema));
    if (!schema) {
        fclose(fp);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *eq;

        line[strcspn(line, "\r\n")] = '\0';

        if (strncmp(line, "table=", 6) == 0) {
            strncpy(schema->table_name, line + 6, sizeof(schema->table_name) - 1);
            continue;
        }

        if (strncmp(line, "columns=", 8) == 0) {
            col_count = atoi(line + 8);
            if (col_count <= 0) {
                fprintf(stderr, "schema: invalid column count in '%s'\n", path);
                schema_free(schema);
                fclose(fp);
                return NULL;
            }

            schema->column_count = col_count;
            schema->columns = (ColDef *)calloc((size_t)col_count, sizeof(ColDef));
            if (!schema->columns) {
                schema_free(schema);
                fclose(fp);
                return NULL;
            }
            continue;
        }

        if (strncmp(line, "col", 3) != 0) continue;

        eq = strchr(line, '=');
        if (!eq || !schema->columns) continue;

        {
            int idx = atoi(line + 3);
            char *value = eq + 1;
            char *fields[16] = {0};
            int field_count = 0;
            int i;

            if (idx < 0 || idx >= col_count) continue;

            field_count = split_schema_fields(value,
                                              fields,
                                              (int)(sizeof(fields) / sizeof(fields[0])));

            if (field_count < 3) continue;

            strncpy(schema->columns[idx].name,
                    fields[0],
                    sizeof(schema->columns[idx].name) - 1);
            schema->columns[idx].max_len = atoi(fields[2]);

            if (strcmp(fields[1], "INT") == 0) {
                schema->columns[idx].type = COL_INT;
            } else if (strcmp(fields[1], "VARCHAR") == 0) {
                schema->columns[idx].type = COL_VARCHAR;
            } else if (strcmp(fields[1], "BOOLEAN") == 0) {
                schema->columns[idx].type = COL_BOOLEAN;
            } else {
                fprintf(stderr,
                        "schema: unknown type '%s' for column '%s'\n",
                        fields[1], fields[0]);
                schema_free(schema);
                fclose(fp);
                return NULL;
            }

            for (i = 3; i < field_count; i++) {
                if (strcmp(fields[i], "PK") == 0 ||
                    strcmp(fields[i], "PRIMARY_KEY") == 0) {
                    schema->columns[idx].is_primary_key = 1;
                    schema->columns[idx].is_unique = 1;
                } else if (strcmp(fields[i], "UNIQUE") == 0) {
                    schema->columns[idx].is_unique = 1;
                } else if (strcmp(fields[i], "AUTO_INCREMENT") == 0) {
                    schema->columns[idx].is_auto_increment = 1;
                } else if (fields[i][0] != '\0') {
                    fprintf(stderr,
                            "schema: unknown attribute '%s' for column '%s'\n",
                            fields[i], fields[0]);
                    schema_free(schema);
                    fclose(fp);
                    return NULL;
                }
            }
        }
    }

    fclose(fp);

    if (schema->column_count == 0 || !schema->columns) {
        fprintf(stderr, "schema: missing column definitions in '%s'\n", path);
        schema_free(schema);
        return NULL;
    }

    return schema;
}

int schema_validate(const ASTNode *node, const TableSchema *schema) {
    if (!node || !schema) return SQL_ERR;

    if (node->type == STMT_INSERT) {
        const InsertStmt *ins = &node->insert;
        int auto_col = find_auto_increment_column(schema);

        if (ins->column_count > 0) {
            int *seen;
            int i;

            if (ins->column_count != ins->value_count) {
                fprintf(stderr,
                        "schema: INSERT column count %d != value count %d\n",
                        ins->column_count, ins->value_count);
                return SQL_ERR;
            }

            seen = (int *)calloc((size_t)schema->column_count, sizeof(int));
            if (!seen) return SQL_ERR;

            for (i = 0; i < ins->column_count; i++) {
                int idx = find_column_index(schema, ins->columns[i]);

                if (idx < 0) {
                    fprintf(stderr,
                            "schema: unknown column '%s' in INSERT\n",
                            ins->columns[i]);
                    free(seen);
                    return SQL_ERR;
                }

                if (seen[idx]) {
                    fprintf(stderr,
                            "schema: duplicate column '%s' in INSERT\n",
                            ins->columns[i]);
                    free(seen);
                    return SQL_ERR;
                }

                if (schema->columns[idx].is_auto_increment) {
                    fprintf(stderr,
                            "schema: column '%s' is AUTO_INCREMENT and cannot "
                            "be inserted explicitly\n",
                            ins->columns[i]);
                    free(seen);
                    return SQL_ERR;
                }

                if (validate_value_for_column(&schema->columns[idx],
                                              ins->values[i]) != SQL_OK) {
                    free(seen);
                    return SQL_ERR;
                }

                seen[idx] = 1;
            }

            for (i = 0; i < schema->column_count; i++) {
                if (schema->columns[i].is_auto_increment) continue;
                if (!seen[i]) {
                    fprintf(stderr,
                            "schema: missing required column '%s' in INSERT\n",
                            schema->columns[i].name);
                    free(seen);
                    return SQL_ERR;
                }
            }

            free(seen);
            return SQL_OK;
        }

        if (auto_col >= 0) {
            fprintf(stderr,
                    "schema: INSERT INTO %s VALUES (...) is not allowed for "
                    "AUTO_INCREMENT tables; use a column list without '%s'\n",
                    schema->table_name, schema->columns[auto_col].name);
            return SQL_ERR;
        }

        if (ins->value_count != schema->column_count) {
            fprintf(stderr,
                    "schema: INSERT expects %d values, got %d\n",
                    schema->column_count, ins->value_count);
            return SQL_ERR;
        }

        {
            int i;
            for (i = 0; i < ins->value_count; i++) {
                if (validate_value_for_column(&schema->columns[i],
                                              ins->values[i]) != SQL_OK) {
                    return SQL_ERR;
                }
            }
        }

        return SQL_OK;
    }

    if (node->type == STMT_SELECT) {
        const SelectStmt *sel = &node->select;
        int i;

        if (!sel->select_all) {
            for (i = 0; i < sel->column_count; i++) {
                if (find_column_index(schema, sel->columns[i]) < 0) {
                    fprintf(stderr,
                            "schema: unknown column '%s' in SELECT\n",
                            sel->columns[i]);
                    return SQL_ERR;
                }
            }
        }

        if (sel->has_where) {
            if (find_column_index(schema, sel->where.col) < 0) {
                fprintf(stderr,
                        "schema: unknown column '%s' in WHERE\n",
                        sel->where.col);
                return SQL_ERR;
            }

            if (sel->where.type == WHERE_BETWEEN) {
                ColType ctype = column_type(schema, sel->where.col);

                if (ctype != COL_INT) {
                    fprintf(stderr,
                            "schema: BETWEEN is only supported on INT "
                            "columns, '%s' is not INT\n",
                            sel->where.col);
                    return SQL_ERR;
                }

                if (!is_integer_string(sel->where.val_from) ||
                    !is_integer_string(sel->where.val_to)) {
                    fprintf(stderr,
                            "schema: BETWEEN values must be integers\n");
                    return SQL_ERR;
                }
            }
        }

        return SQL_OK;
    }

    fprintf(stderr, "schema: unknown statement type\n");
    return SQL_ERR;
}

void schema_free(TableSchema *schema) {
    if (!schema) return;

    free(schema->columns);
    free(schema);
}
