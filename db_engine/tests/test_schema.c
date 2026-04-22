#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/interface.h"

/*
 * schema validation 테스트다.
 * schema 파일 기준으로 SELECT/INSERT가 컬럼 타입, 필수 컬럼,
 * AUTO_INCREMENT 규칙을 지키는지 확인한다.
 */

static int failures = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg) do {                                      \
    if (!(cond)) {                                                       \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg);  \
        failures++;                                                      \
        goto cleanup;                                                    \
    }                                                                    \
} while (0)

static void run_test(const char *name, void (*fn)(void)) {
    int before = failures;
    fn();
    tests_run++;
    if (failures == before) printf("[PASS] %s\n", name);
}

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

static ASTNode *make_between_select(const char *col,
                                    const char *from,
                                    const char *to) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) return NULL;

    node->type = STMT_SELECT;
    node->select.select_all = 1;
    node->select.has_where = 1;
    node->select.where.type = WHERE_BETWEEN;

    copy_text(node->select.table, sizeof(node->select.table), "users");
    copy_text(node->select.where.col, sizeof(node->select.where.col), col);
    copy_text(node->select.where.val_from,
              sizeof(node->select.where.val_from), from);
    copy_text(node->select.where.val_to,
              sizeof(node->select.where.val_to), to);
    return node;
}

static ASTNode *make_eq_select(const char *col, const char *val) {
    ASTNode *node = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!node) return NULL;

    node->type = STMT_SELECT;
    node->select.select_all = 1;
    node->select.has_where = 1;
    node->select.where.type = WHERE_EQ;

    copy_text(node->select.table, sizeof(node->select.table), "users");
    copy_text(node->select.where.col, sizeof(node->select.where.col), col);
    copy_text(node->select.where.val, sizeof(node->select.where.val), val);
    return node;
}

static void init_insert_node(ASTNode *node,
                             char **columns,
                             int column_count,
                             char **values,
                             int value_count) {
    memset(node, 0, sizeof(*node));
    node->type = STMT_INSERT;
    copy_text(node->insert.table, sizeof(node->insert.table), "users");
    node->insert.columns = columns;
    node->insert.column_count = column_count;
    node->insert.values = values;
    node->insert.value_count = value_count;
}

static void test_schema_load_users(void) {
    TableSchema *schema = NULL;

    schema = schema_load("users");
    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(strcmp(schema->table_name, "users") == 0,
                "table_name should be users");
    EXPECT_TRUE(schema->column_count == 4, "users schema should have 4 columns");
    EXPECT_TRUE(strcmp(schema->columns[0].name, "id") == 0,
                "first column should be id");
    EXPECT_TRUE(schema->columns[0].type == COL_INT, "id should be INT");
    EXPECT_TRUE(schema->columns[0].is_primary_key == 1,
                "id should be primary key");
    EXPECT_TRUE(schema->columns[0].is_unique == 1,
                "id should be unique");
    EXPECT_TRUE(schema->columns[0].is_auto_increment == 1,
                "id should be auto increment");

cleanup:
    schema_free(schema);
}

static void test_validate_auto_insert_with_column_list(void) {
    TableSchema *schema = NULL;
    ASTNode node;
    char *columns[] = { "name", "age", "email" };
    char *values[] = { "alice", "25", "alice@example.com" };

    schema = schema_load("users");
    init_insert_node(&node, columns, 3, values, 3);

    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(schema_validate(&node, schema) == SQL_OK,
                "column-list insert without id should validate");

cleanup:
    schema_free(schema);
}

static void test_validate_rejects_explicit_auto_id(void) {
    TableSchema *schema = NULL;
    ASTNode node;
    char *columns[] = { "id", "name", "age", "email" };
    char *values[] = { "1", "alice", "25", "alice@example.com" };

    schema = schema_load("users");
    init_insert_node(&node, columns, 4, values, 4);

    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(schema_validate(&node, schema) == SQL_ERR,
                "explicit AUTO_INCREMENT id should be rejected");

cleanup:
    schema_free(schema);
}

static void test_validate_rejects_values_insert_for_auto_table(void) {
    TableSchema *schema = NULL;
    ASTNode node;
    char *values[] = { "1", "alice", "25", "alice@example.com" };

    schema = schema_load("users");
    init_insert_node(&node, NULL, 0, values, 4);

    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(schema_validate(&node, schema) == SQL_ERR,
                "VALUES-only insert should be rejected for auto-id table");

cleanup:
    schema_free(schema);
}

static void test_validate_rejects_missing_required_column(void) {
    TableSchema *schema = NULL;
    ASTNode node;
    char *columns[] = { "name", "age" };
    char *values[] = { "alice", "25" };

    schema = schema_load("users");
    init_insert_node(&node, columns, 2, values, 2);

    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(schema_validate(&node, schema) == SQL_ERR,
                "missing non-auto column should be rejected");

cleanup:
    schema_free(schema);
}

static void test_validate_between_on_int_column(void) {
    TableSchema *schema = NULL;
    ASTNode *node = NULL;

    schema = schema_load("users");
    node = make_between_select("id", "1", "100");
    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(node != NULL, "AST allocation should succeed");
    EXPECT_TRUE(schema_validate(node, schema) == SQL_OK,
                "BETWEEN on INT column should validate");

cleanup:
    schema_free(schema);
    free(node);
}

static void test_validate_between_rejects_varchar_column(void) {
    TableSchema *schema = NULL;
    ASTNode *node = NULL;

    schema = schema_load("users");
    node = make_between_select("name", "a", "z");
    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(node != NULL, "AST allocation should succeed");
    EXPECT_TRUE(schema_validate(node, schema) == SQL_ERR,
                "BETWEEN on VARCHAR column should fail");

cleanup:
    schema_free(schema);
    free(node);
}

static void test_validate_between_rejects_non_integer_bounds(void) {
    TableSchema *schema = NULL;
    ASTNode *node = NULL;

    schema = schema_load("users");
    node = make_between_select("id", "abc", "100");
    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(node != NULL, "AST allocation should succeed");
    EXPECT_TRUE(schema_validate(node, schema) == SQL_ERR,
                "BETWEEN bounds must be integer strings");

cleanup:
    schema_free(schema);
    free(node);
}

static void test_validate_eq_on_varchar_column(void) {
    TableSchema *schema = NULL;
    ASTNode *node = NULL;

    schema = schema_load("users");
    node = make_eq_select("name", "alice");
    EXPECT_TRUE(schema != NULL, "schema_load(users) should succeed");
    EXPECT_TRUE(node != NULL, "AST allocation should succeed");
    EXPECT_TRUE(schema_validate(node, schema) == SQL_OK,
                "WHERE_EQ on VARCHAR column should still validate");

cleanup:
    schema_free(schema);
    free(node);
}

int main(void) {
    run_test("schema loads users definition", test_schema_load_users);
    run_test("schema validates auto insert with column list",
             test_validate_auto_insert_with_column_list);
    run_test("schema rejects explicit auto id",
             test_validate_rejects_explicit_auto_id);
    run_test("schema rejects values-only insert for auto table",
             test_validate_rejects_values_insert_for_auto_table);
    run_test("schema rejects missing required column",
             test_validate_rejects_missing_required_column);
    run_test("schema validates BETWEEN on INT", test_validate_between_on_int_column);
    run_test("schema rejects BETWEEN on VARCHAR",
             test_validate_between_rejects_varchar_column);
    run_test("schema rejects non-integer BETWEEN bounds",
             test_validate_between_rejects_non_integer_bounds);
    run_test("schema still allows equality on VARCHAR",
             test_validate_eq_on_varchar_column);

    if (failures > 0) {
        fprintf(stderr, "\n%d/%d schema tests failed.\n", failures, tests_run);
        return 1;
    }

    printf("\nAll %d schema tests passed.\n", tests_run);
    return 0;
}
