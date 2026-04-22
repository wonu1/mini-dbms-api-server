#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  define MKDIR(p) mkdir(p, 0755)
#endif

#include "../include/index_manager.h"
#include "../include/interface.h"

#define TEST_TABLE "executor_test_users"

static int g_failures = 0;

static void copy_text(char *dst, size_t dst_size, const char *src) {
    size_t len;

    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void report_failure(const char *test_name, const char *message) {
    fprintf(stderr, "[FAIL] %s: %s\n", test_name, message);
    g_failures++;
}

static void build_data_path(char *buf, size_t size) {
    snprintf(buf, size, "data/%s.dat", TEST_TABLE);
}

static void build_schema_path(char *buf, size_t size) {
    snprintf(buf, size, "schema/%s.schema", TEST_TABLE);
}

static void remove_if_exists(const char *path) {
    if (!path) return;
    remove(path);
}

static int write_schema_file(void) {
    char schema_path[256];
    FILE *fp;

    build_schema_path(schema_path, sizeof(schema_path));
    fp = fopen(schema_path, "wb");
    if (!fp) return 0;

    fprintf(fp,
            "table=%s\n"
            "columns=4\n"
            "col0=id,INT,0,PK,AUTO_INCREMENT\n"
            "col1=name,VARCHAR,64\n"
            "col2=age,INT,0\n"
            "col3=email,VARCHAR,128\n",
            TEST_TABLE);
    fclose(fp);
    return 1;
}

static void reset_fixture(void) {
    char data_path[256];
    char schema_path[256];

    MKDIR("data");
    MKDIR("schema");

    build_data_path(data_path, sizeof(data_path));
    build_schema_path(schema_path, sizeof(schema_path));

    index_cleanup();
    remove_if_exists(data_path);
    remove_if_exists(schema_path);
    write_schema_file();
}

static void cleanup_fixture(void) {
    char data_path[256];
    char schema_path[256];

    build_data_path(data_path, sizeof(data_path));
    build_schema_path(schema_path, sizeof(schema_path));

    index_cleanup();
    remove_if_exists(data_path);
    remove_if_exists(schema_path);
}

static void init_insert_stmt(InsertStmt *stmt,
                             char **columns,
                             int column_count,
                             char **values,
                             int value_count) {
    memset(stmt, 0, sizeof(*stmt));
    copy_text(stmt->table, sizeof(stmt->table), TEST_TABLE);
    stmt->columns = columns;
    stmt->column_count = column_count;
    stmt->values = values;
    stmt->value_count = value_count;
}

static void init_select_eq(SelectStmt *stmt, const char *col, const char *value) {
    memset(stmt, 0, sizeof(*stmt));
    stmt->select_all = 1;
    copy_text(stmt->table, sizeof(stmt->table), TEST_TABLE);
    stmt->has_where = 1;
    copy_text(stmt->where.col, sizeof(stmt->where.col), col);
    stmt->where.type = WHERE_EQ;
    copy_text(stmt->where.val, sizeof(stmt->where.val), value);
}

static int validate_insert_stmt(const InsertStmt *stmt, const TableSchema *schema) {
    ASTNode node;
    memset(&node, 0, sizeof(node));
    node.type = STMT_INSERT;
    node.insert = *stmt;
    return schema_validate(&node, schema);
}

static int validate_select_stmt(const SelectStmt *stmt, const TableSchema *schema) {
    ASTNode node;
    memset(&node, 0, sizeof(node));
    node.type = STMT_SELECT;
    node.select = *stmt;
    return schema_validate(&node, schema);
}

static int insert_user(const TableSchema *schema,
                       const char *name,
                       const char *age,
                       const char *email) {
    InsertStmt stmt;
    char *columns[] = { "name", "age", "email" };
    char *values[] = { (char *)name, (char *)age, (char *)email };

    init_insert_stmt(&stmt, columns, 3, values, 3);

    if (validate_insert_stmt(&stmt, schema) != SQL_OK) return SQL_ERR;
    return db_insert(&stmt, schema);
}

static ResultSet *select_eq_checked(const TableSchema *schema,
                                    const char *col,
                                    const char *value) {
    SelectStmt stmt;

    init_select_eq(&stmt, col, value);
    if (validate_select_stmt(&stmt, schema) != SQL_OK) return NULL;
    return db_select(&stmt, schema);
}

static void test_auto_insert_assigns_sequential_ids(void) {
    const char *test_name = "auto_insert_assigns_sequential_ids";
    TableSchema *schema = NULL;
    ResultSet *by_name = NULL;
    ResultSet *by_id = NULL;

    reset_fixture();
    schema = schema_load(TEST_TABLE);
    if (!schema) {
        report_failure(test_name, "schema_load failed");
        goto cleanup;
    }

    if (index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) != 0) {
        report_failure(test_name, "index_init failed");
        goto cleanup;
    }

    if (insert_user(schema, "alice", "25", "alice@example.com") != SQL_OK ||
        insert_user(schema, "bob", "32", "bob@example.com") != SQL_OK) {
        report_failure(test_name, "auto inserts failed");
        goto cleanup;
    }

    by_name = select_eq_checked(schema, "name", "alice");
    by_id = select_eq_checked(schema, "id", "2");
    if (!by_name || !by_id) {
        report_failure(test_name, "select failed");
        goto cleanup;
    }

    if (by_name->row_count != 1 ||
        strcmp(by_name->rows[0].values[0], "1") != 0 ||
        strcmp(by_name->rows[0].values[1], "alice") != 0 ||
        by_id->row_count != 1 ||
        strcmp(by_id->rows[0].values[1], "bob") != 0) {
        report_failure(test_name, "auto-generated ids were not sequential");
    }

cleanup:
    result_free(by_name);
    result_free(by_id);
    schema_free(schema);
    cleanup_fixture();
}

static void test_restart_resumes_next_auto_id(void) {
    const char *test_name = "restart_resumes_next_auto_id";
    TableSchema *schema = NULL;
    ResultSet *by_id = NULL;

    reset_fixture();
    schema = schema_load(TEST_TABLE);
    if (!schema) {
        report_failure(test_name, "schema_load failed");
        goto cleanup;
    }

    if (index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) != 0) {
        report_failure(test_name, "initial index_init failed");
        goto cleanup;
    }

    if (insert_user(schema, "first", "21", "first@example.com") != SQL_OK) {
        report_failure(test_name, "first insert failed");
        goto cleanup;
    }

    index_cleanup();
    if (index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) != 0) {
        report_failure(test_name, "reinit failed");
        goto cleanup;
    }

    if (insert_user(schema, "second", "22", "second@example.com") != SQL_OK) {
        report_failure(test_name, "second insert failed");
        goto cleanup;
    }

    by_id = select_eq_checked(schema, "id", "2");
    if (!by_id) {
        report_failure(test_name, "select by id failed");
        goto cleanup;
    }

    if (by_id->row_count != 1 ||
        strcmp(by_id->rows[0].values[1], "second") != 0) {
        report_failure(test_name, "next_auto_id did not resume after restart");
    }

cleanup:
    result_free(by_id);
    schema_free(schema);
    cleanup_fixture();
}

static void test_invalid_insert_patterns_are_rejected(void) {
    const char *test_name = "invalid_insert_patterns_are_rejected";
    TableSchema *schema = NULL;
    InsertStmt values_only;
    InsertStmt explicit_id;
    char *values_only_values[] = { "1", "alice", "25", "alice@example.com" };
    char *explicit_columns[] = { "id", "name", "age", "email" };
    char *explicit_values[] = { "1", "alice", "25", "alice@example.com" };

    reset_fixture();
    schema = schema_load(TEST_TABLE);
    if (!schema) {
        report_failure(test_name, "schema_load failed");
        goto cleanup;
    }

    init_insert_stmt(&values_only, NULL, 0, values_only_values, 4);
    init_insert_stmt(&explicit_id, explicit_columns, 4, explicit_values, 4);

    if (validate_insert_stmt(&values_only, schema) != SQL_ERR ||
        validate_insert_stmt(&explicit_id, schema) != SQL_ERR) {
        report_failure(test_name, "invalid inserts were accepted");
    }

cleanup:
    schema_free(schema);
    cleanup_fixture();
}

int main(void) {
    test_auto_insert_assigns_sequential_ids();
    test_restart_resumes_next_auto_id();
    test_invalid_insert_patterns_are_rejected();

    if (g_failures == 0) {
        printf("test_executor: PASS\n");
        return 0;
    }

    printf("test_executor: FAIL (%d)\n", g_failures);
    return 1;
}
