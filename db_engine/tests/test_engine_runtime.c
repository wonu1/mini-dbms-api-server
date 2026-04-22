#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/engine_api.h"
#include "../include/index_manager.h"

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(path) _mkdir(path)
#else
#  include <sys/stat.h>
#  define MKDIR(path) mkdir(path, 0755)
#endif

#define AUTO_TABLE "runtime_auto_users"
#define PLAIN_TABLE "runtime_plain_users"
#define FIXTURE_ROOT_DIR "db_engine/runtime_fixtures"
#define FIXTURE_SCHEMA_DIR FIXTURE_ROOT_DIR "/schema"
#define FIXTURE_DATA_DIR FIXTURE_ROOT_DIR "/data"

static int g_failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        g_failures++;
    }
}

static void build_schema_path(char *buffer, size_t size, const char *table_name) {
    snprintf(buffer, size, FIXTURE_SCHEMA_DIR "/%s.schema", table_name);
}

static void build_data_path(char *buffer, size_t size, const char *table_name) {
    snprintf(buffer, size, FIXTURE_DATA_DIR "/%s.dat", table_name);
}

static void remove_if_exists(const char *path) {
    if (!path) return;
    remove(path);
}

static int write_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;

    fputs(contents, file);
    fclose(file);
    return 1;
}

static void reset_fixtures(void) {
    char auto_schema[256];
    char plain_schema[256];
    char auto_data[256];
    char plain_data[256];

    MKDIR(FIXTURE_ROOT_DIR);
    MKDIR(FIXTURE_SCHEMA_DIR);
    MKDIR(FIXTURE_DATA_DIR);

    build_schema_path(auto_schema, sizeof(auto_schema), AUTO_TABLE);
    build_schema_path(plain_schema, sizeof(plain_schema), PLAIN_TABLE);
    build_data_path(auto_data, sizeof(auto_data), AUTO_TABLE);
    build_data_path(plain_data, sizeof(plain_data), PLAIN_TABLE);

    index_cleanup();
    remove_if_exists(auto_schema);
    remove_if_exists(plain_schema);
    remove_if_exists(auto_data);
    remove_if_exists(plain_data);

    write_file(auto_schema,
               "table=" AUTO_TABLE "\n"
               "columns=4\n"
               "col0=id,INT,0,PK,AUTO_INCREMENT\n"
               "col1=name,VARCHAR,64\n"
               "col2=age,INT,0\n"
               "col3=email,VARCHAR,128\n");
    write_file(plain_schema,
               "table=" PLAIN_TABLE "\n"
               "columns=2\n"
               "col0=id,INT,0,PK\n"
               "col1=name,VARCHAR,64\n");
}

static void cleanup_fixtures(void) {
    char auto_schema[256];
    char plain_schema[256];
    char auto_data[256];
    char plain_data[256];

    build_schema_path(auto_schema, sizeof(auto_schema), AUTO_TABLE);
    build_schema_path(plain_schema, sizeof(plain_schema), PLAIN_TABLE);
    build_data_path(auto_data, sizeof(auto_data), AUTO_TABLE);
    build_data_path(plain_data, sizeof(plain_data), PLAIN_TABLE);

    index_cleanup();
    remove_if_exists(auto_schema);
    remove_if_exists(plain_schema);
    remove_if_exists(auto_data);
    remove_if_exists(plain_data);
}

static void free_error(char **message) {
    free(*message);
    *message = NULL;
}

static void test_runtime_state(void) {
    const EngineRuntimeState *state;

    reset_fixtures();
    expect_true(engine_runtime_init() == ENGINE_RUNTIME_OK,
                "engine_runtime_init should succeed");
    state = engine_runtime_get_state();
    expect_true(state->initialized == 1,
                "runtime should report initialized after init");
    expect_true(strcmp(state->schema_dir, "db_engine/schema") == 0,
                "default schema dir should be stored");
    expect_true(state->lock_ready == 1,
                "runtime should prepare lock state during init");
    expect_true(engine_runtime_set_schema_dir(FIXTURE_SCHEMA_DIR) == ENGINE_RUNTIME_OK,
                "setting schema dir should succeed");
    expect_true(engine_runtime_prepare_all() == ENGINE_RUNTIME_OK,
                "prepare_all should preload fixture indexes");
    state = engine_runtime_get_state();
    expect_true(state->prepared == 1,
                "prepare_all should mark runtime as prepared");
    expect_true(strcmp(state->schema_dir, FIXTURE_SCHEMA_DIR) == 0,
                "runtime should store overridden fixture schema dir");
    engine_runtime_shutdown();
    state = engine_runtime_get_state();
    expect_true(state->initialized == 0,
                "runtime shutdown should reset initialized state");
    cleanup_fixtures();
}

static void test_auto_insert_and_select(void) {
    EngineResponse response = {0};
    EngineErrorCode error_code = ENGINE_ERR_RUNTIME;
    char *error_message = NULL;

    reset_fixtures();
    expect_true(engine_runtime_init() == ENGINE_RUNTIME_OK,
                "runtime init should succeed for auto fixture");
    expect_true(engine_runtime_set_schema_dir(FIXTURE_SCHEMA_DIR) == ENGINE_RUNTIME_OK,
                "fixture schema dir should be applied before auto insert");

    expect_true(engine_execute_sql(
                    "INSERT INTO " AUTO_TABLE " (name, age, email) "
                    "VALUES ('alice', 25, 'alice@example.com');",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_OK,
                "auto insert should succeed");
    expect_true(error_message == NULL, "successful insert should not set error_message");
    expect_true(response.type == ENGINE_RESULT_INSERT,
                "insert response should have INSERT type");
    expect_true(response.insert.affected_rows == 1,
                "insert response should report one affected row");
    expect_true(response.insert.has_generated_id == 1,
                "auto insert should expose generated id");
    expect_true(response.insert.generated_id == 1,
                "first generated id should be 1");
    expect_true(engine_runtime_get_state()->prepared == 1,
                "first execute should lazily prepare runtime state");
    engine_response_free(&response);

    expect_true(engine_execute_sql(
                    "INSERT INTO " AUTO_TABLE " (name, age, email) "
                    "VALUES ('bob', 32, 'bob@example.com');",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_OK,
                "second auto insert should succeed");
    expect_true(response.insert.generated_id == 2,
                "second generated id should be 2");
    engine_response_free(&response);

    expect_true(engine_execute_sql(
                    "SELECT id, name FROM " AUTO_TABLE " WHERE id BETWEEN 1 AND 2;",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_OK,
                "select after inserts should succeed");
    expect_true(response.type == ENGINE_RESULT_SELECT,
                "select response should have SELECT type");
    expect_true(response.select.column_count == 2,
                "select should return requested columns only");
    expect_true(response.select.row_count == 2,
                "select should return two rows");
    expect_true(strcmp(response.select.columns[0], "id") == 0,
                "first selected column should be id");
    expect_true(strcmp(response.select.columns[1], "name") == 0,
                "second selected column should be name");
    expect_true(strcmp(response.select.rows[0][0], "1") == 0,
                "first row should contain generated id 1");
    expect_true(strcmp(response.select.rows[0][1], "alice") == 0,
                "first row should contain alice");
    expect_true(strcmp(response.select.rows[1][0], "2") == 0,
                "second row should contain generated id 2");
    expect_true(strcmp(response.select.rows[1][1], "bob") == 0,
                "second row should contain bob");
    engine_response_free(&response);

    engine_runtime_shutdown();
    cleanup_fixtures();
}

static void test_plain_insert_without_generated_id(void) {
    EngineResponse response = {0};
    EngineErrorCode error_code = ENGINE_ERR_RUNTIME;
    char *error_message = NULL;

    reset_fixtures();
    expect_true(engine_runtime_init() == ENGINE_RUNTIME_OK,
                "runtime init should succeed for plain fixture");
    expect_true(engine_runtime_set_schema_dir(FIXTURE_SCHEMA_DIR) == ENGINE_RUNTIME_OK,
                "fixture schema dir should be applied before plain insert");
    expect_true(engine_execute_sql(
                    "INSERT INTO " PLAIN_TABLE " VALUES (10, 'plain-user');",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_OK,
                "plain insert should succeed");
    expect_true(response.type == ENGINE_RESULT_INSERT,
                "plain insert should still produce INSERT response");
    expect_true(response.insert.has_generated_id == 0,
                "non-auto insert should not expose generated id");
    expect_true(response.insert.generated_id == 0,
                "non-auto insert should report generated_id 0");
    engine_response_free(&response);
    engine_runtime_shutdown();
    cleanup_fixtures();
}

static void test_parse_failures(void) {
    EngineResponse response = {0};
    EngineErrorCode error_code = ENGINE_ERR_RUNTIME;
    char *error_message = NULL;

    reset_fixtures();
    expect_true(engine_runtime_init() == ENGINE_RUNTIME_OK,
                "runtime init should succeed for parse fixture");
    expect_true(engine_runtime_set_schema_dir(FIXTURE_SCHEMA_DIR) == ENGINE_RUNTIME_OK,
                "fixture schema dir should be applied before parse checks");

    expect_true(engine_execute_sql("   ", &response, &error_code, &error_message) ==
                    ENGINE_API_ERR,
                "blank SQL should fail");
    expect_true(error_code == ENGINE_ERR_PARSE,
                "blank SQL should map to parse error");
    free_error(&error_message);

    expect_true(engine_execute_sql(
                    "SELECT * FROM users; SELECT * FROM users;",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_ERR,
                "multiple statements should fail");
    expect_true(error_code == ENGINE_ERR_PARSE,
                "multiple statements should map to parse error");
    free_error(&error_message);

    expect_true(engine_execute_sql(
                    "SELECT FROM " AUTO_TABLE ";",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_ERR,
                "invalid SQL should fail");
    expect_true(error_code == ENGINE_ERR_PARSE,
                "invalid SQL should map to parse error");
    free_error(&error_message);

    engine_runtime_shutdown();
    cleanup_fixtures();
}

static void test_validation_failures(void) {
    EngineResponse response = {0};
    EngineErrorCode error_code = ENGINE_ERR_RUNTIME;
    char *error_message = NULL;

    reset_fixtures();
    expect_true(engine_runtime_init() == ENGINE_RUNTIME_OK,
                "runtime init should succeed for validation fixture");
    expect_true(engine_runtime_set_schema_dir(FIXTURE_SCHEMA_DIR) == ENGINE_RUNTIME_OK,
                "fixture schema dir should be applied before validation checks");

    expect_true(engine_execute_sql(
                    "SELECT * FROM missing_table;",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_ERR,
                "missing schema should fail");
    expect_true(error_code == ENGINE_ERR_VALIDATION,
                "missing schema should map to validation");
    free_error(&error_message);

    expect_true(engine_execute_sql(
                    "SELECT unknown FROM " AUTO_TABLE ";",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_ERR,
                "unknown column should fail validation");
    expect_true(error_code == ENGINE_ERR_VALIDATION,
                "unknown column should map to validation");
    free_error(&error_message);

    expect_true(engine_execute_sql(
                    "INSERT INTO " AUTO_TABLE " VALUES (1, 'bad', 25, 'bad@example.com');",
                    &response,
                    &error_code,
                    &error_message) == ENGINE_API_ERR,
                "invalid insert shape should fail validation");
    expect_true(error_code == ENGINE_ERR_VALIDATION,
                "invalid insert shape should map to validation");
    free_error(&error_message);

    engine_runtime_shutdown();
    cleanup_fixtures();
}

int main(void) {
    test_runtime_state();
    test_auto_insert_and_select();
    test_plain_insert_without_generated_id();
    test_parse_failures();
    test_validation_failures();

    if (g_failures != 0) {
        printf("test_engine_runtime: FAIL (%d)\n", g_failures);
        return 1;
    }

    printf("test_engine_runtime: PASS\n");
    return 0;
}
