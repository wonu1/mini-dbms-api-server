/*
 * 인덱스 매니저 테스트다.
 * 메모리 인덱스 조작과 .dat 파일 로딩 동작을 같이 확인한다.
 */

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

#include "../include/interface.h"
#include "../include/index_manager.h"

/* 다른 테스트와 충돌하지 않도록 고정된 테스트용 테이블명을 쓴다. */
#define TEST_TABLE "test_idx_users"

static int failures  = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg) do {                                       \
    if (!(cond)) {                                                        \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        failures++;                                                       \
        goto cleanup;                                                     \
    }                                                                     \
} while (0)

static void run_test(const char *name, void (*fn)(void)) {
    int before = failures;
    fn();
    tests_run++;
    if (failures == before)
        printf("[PASS] %s\n", name);
}

/* 테스트용 파일 준비 */

static void build_data_path(char *buf, size_t size) {
    snprintf(buf, size, "data/%s.dat", TEST_TABLE);
}

static void remove_if_exists(const char *path) {
    if (path) remove(path);
}

/* 한 줄을 파일에 기록하고, 그 줄이 시작되는 오프셋을 돌려준다. */
static long write_dat_row(FILE *fp,
                          int id, const char *name,
                          int age, const char *email) {
    long offset = ftell(fp);
    fprintf(fp, "%d | %s | %d | %s\n", id, name, age, email);
    return offset;
}

static void reset_fixture(void) {
    char data_path[256];
    build_data_path(data_path, sizeof(data_path));
    index_cleanup();
    remove_if_exists(data_path);
    MKDIR("data");
}

static void cleanup_fixture(void) {
    char data_path[256];
    build_data_path(data_path, sizeof(data_path));
    index_cleanup();
    remove_if_exists(data_path);
}

/* 초기화 */

static void test_init_no_dat_file_succeeds(void) {
    int result;
    reset_fixture();
    result = index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT);
    EXPECT_TRUE(result == 0,
                "index_init should succeed even when .dat file is missing");
cleanup:
    cleanup_fixture();
}

static void test_init_idempotent(void) {
    int r1;
    int r2;
    reset_fixture();
    r1 = index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT);
    r2 = index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT);
    EXPECT_TRUE(r1 == 0, "first index_init should return 0");
    EXPECT_TRUE(r2 == 0, "second index_init (idempotent) should return 0");
cleanup:
    cleanup_fixture();
}

/* id 인덱스 */

static void test_insert_id_and_search(void) {
    long result;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    EXPECT_TRUE(index_insert_id(TEST_TABLE, 42, 1000L) == 0,
                "index_insert_id should return 0");
    result = index_search_id(TEST_TABLE, 42);
    EXPECT_TRUE(result == 1000L,
                "index_search_id should return the inserted offset");
cleanup:
    cleanup_fixture();
}

static void test_search_missing_id_returns_neg1(void) {
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    index_insert_id(TEST_TABLE, 1, 100L);
    EXPECT_TRUE(index_search_id(TEST_TABLE, 999) == -1,
                "search for absent id should return -1");
cleanup:
    cleanup_fixture();
}

static void test_search_uninitialized_table_returns_neg1(void) {
    reset_fixture();
    EXPECT_TRUE(index_search_id("unknown_table", 1) == -1,
                "search on uninitialized table should return -1");
cleanup:
    cleanup_fixture();
}

static void test_range_id_basic(void) {
    long offsets[10];
    int  i;
    int  count;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    for (i = 1; i <= 5; i++)
        index_insert_id(TEST_TABLE, i, (long)(i * 100));
    count = index_range_id(TEST_TABLE, 1, 5, offsets, 10);
    EXPECT_TRUE(count == 5, "range [1,5] should return 5 results");
cleanup:
    cleanup_fixture();
}

static void test_range_id_empty_returns_zero(void) {
    long offsets[10];
    int  count;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    index_insert_id(TEST_TABLE, 1, 100L);
    index_insert_id(TEST_TABLE, 2, 200L);
    count = index_range_id(TEST_TABLE, 50, 100, offsets, 10);
    EXPECT_TRUE(count == 0, "range outside all keys should return 0");
cleanup:
    cleanup_fixture();
}

static void test_range_id_boundary_inclusive(void) {
    long offsets[10];
    int  i;
    int  count;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    for (i = 1; i <= 10; i++)
        index_insert_id(TEST_TABLE, i, (long)(i * 100));
    count = index_range_id(TEST_TABLE, 3, 7, offsets, 10);
    EXPECT_TRUE(count == 5,
                "range [3,7] should include both boundary ids (3 and 7)");
cleanup:
    cleanup_fixture();
}

/* age 인덱스 */

static void test_insert_age_and_range(void) {
    long offsets[10];
    int  count;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    index_insert_age(TEST_TABLE, 25, 500L);
    count = index_range_age(TEST_TABLE, 20, 30, offsets, 10);
    EXPECT_TRUE(count == 1,
                "range_age [20,30] should return 1 result for age=25");
    EXPECT_TRUE(offsets[0] == 500L,
                "range_age should return the inserted offset");
cleanup:
    cleanup_fixture();
}

static void test_age_range_duplicate_age(void) {
    long offsets[10];
    int  count;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    index_insert_age(TEST_TABLE, 30, 100L);
    index_insert_age(TEST_TABLE, 30, 200L);
    count = index_range_age(TEST_TABLE, 28, 32, offsets, 10);
    EXPECT_TRUE(count == 2,
                "duplicate age should yield 2 offsets in range_age result");
cleanup:
    cleanup_fixture();
}

/* 정리 후 재초기화 */

static void test_cleanup_and_reinit(void) {
    long result;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "first index_init should succeed");
    index_insert_id(TEST_TABLE, 7, 777L);

    index_cleanup();

    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "reinit after cleanup should succeed");
    result = index_search_id(TEST_TABLE, 7);
    EXPECT_TRUE(result == -1,
                "after cleanup+reinit (no .dat file), old key should not exist");
cleanup:
    cleanup_fixture();
}

/* 기존 .dat 파일 로딩 */

static void test_init_loads_dat_file(void) {
    char  data_path[256];
    FILE *fp = NULL;
    long  offset_id1 = -1;
    long  offset_id2 = -1;
    long  found;
    reset_fixture();
    build_data_path(data_path, sizeof(data_path));

    fp = fopen(data_path, "wb");
    EXPECT_TRUE(fp != NULL, "should be able to create test .dat file");
    offset_id1 = write_dat_row(fp, 1, "alice", 25, "alice@example.com");
    offset_id2 = write_dat_row(fp, 2, "bob",   30, "bob@example.com");
    fclose(fp);
    fp = NULL;

    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed with existing .dat file");

    found = index_search_id(TEST_TABLE, 1);
    EXPECT_TRUE(found == offset_id1,
                "id=1 should map to the offset of the first line");

    found = index_search_id(TEST_TABLE, 2);
    EXPECT_TRUE(found == offset_id2,
                "id=2 should map to the offset of the second line");

cleanup:
    if (fp) fclose(fp);
    cleanup_fixture();
}

static void test_init_loads_duplicate_ages(void) {
    char  data_path[256];
    FILE *fp = NULL;
    long  offsets[10];
    int   count;
    reset_fixture();
    build_data_path(data_path, sizeof(data_path));

    fp = fopen(data_path, "wb");
    EXPECT_TRUE(fp != NULL, "should be able to create test .dat file");
    write_dat_row(fp, 1, "alice", 25, "a@example.com");
    write_dat_row(fp, 2, "bob",   25, "b@example.com");
    write_dat_row(fp, 3, "carol", 30, "c@example.com");
    fclose(fp);
    fp = NULL;

    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");

    count = index_range_age(TEST_TABLE, 25, 25, offsets, 10);
    EXPECT_TRUE(count == 2,
                "age=25 appears twice, range_age should return 2 offsets");

cleanup:
    if (fp) fclose(fp);
    cleanup_fixture();
}

static void test_init_rejects_duplicate_ids(void) {
    char  data_path[256];
    FILE *fp = NULL;

    reset_fixture();
    build_data_path(data_path, sizeof(data_path));

    fp = fopen(data_path, "wb");
    EXPECT_TRUE(fp != NULL, "should be able to create test .dat file");
    write_dat_row(fp, 1, "alice", 25, "a@example.com");
    write_dat_row(fp, 1, "bob",   30, "b@example.com");
    fclose(fp);
    fp = NULL;

    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == -1,
                "index_init should fail when existing data has duplicate ids");

cleanup:
    if (fp) fclose(fp);
    cleanup_fixture();
}

/* 한계 조건 */

static void test_max_tables_exceeded(void) {
    char  tbl[64];
    int   i;
    int   result;

    index_cleanup();
    for (i = 0; i < IDX_MAX_TABLES; i++) {
        snprintf(tbl, sizeof(tbl), "overflow_tbl_%d", i);
        result = index_init(tbl, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT);
        EXPECT_TRUE(result == 0,
                    "init within limit should succeed");
    }

    snprintf(tbl, sizeof(tbl), "overflow_tbl_%d", IDX_MAX_TABLES);
    result = index_init(tbl, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT);
    EXPECT_TRUE(result == -1,
                "init beyond IDX_MAX_TABLES should return -1");
cleanup:
    index_cleanup();
}

static void test_range_id_alloc_basic(void) {
    int   i;
    int   count = 0;
    long *offsets;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    for (i = 1; i <= 5; i++)
        index_insert_id(TEST_TABLE, i, (long)(i * 100));
    offsets = index_range_id_alloc(TEST_TABLE, 2, 4, &count);
    EXPECT_TRUE(count == 3,
                "range_id_alloc [2,4] should return count=3");
    EXPECT_TRUE(offsets != NULL,
                "range_id_alloc should return non-NULL for non-empty result");
cleanup:
    free(offsets);
    cleanup_fixture();
}

static void test_range_age_alloc_basic(void) {
    int   count = 0;
    long *offsets;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    index_insert_age(TEST_TABLE, 25, 100L);
    index_insert_age(TEST_TABLE, 28, 200L);
    index_insert_age(TEST_TABLE, 35, 300L);
    offsets = index_range_age_alloc(TEST_TABLE, 24, 30, &count);
    EXPECT_TRUE(count == 2,
                "range_age_alloc [24,30] should return count=2 for ages 25 and 28");
    EXPECT_TRUE(offsets != NULL,
                "range_age_alloc should return non-NULL for non-empty result");
cleanup:
    free(offsets);
    cleanup_fixture();
}

static void test_range_id_alloc_empty_returns_null(void) {
    int   count = 99;
    long *offsets;
    reset_fixture();
    EXPECT_TRUE(index_init(TEST_TABLE, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) == 0,
                "index_init should succeed");
    index_insert_id(TEST_TABLE, 1, 100L);
    offsets = index_range_id_alloc(TEST_TABLE, 50, 100, &count);
    EXPECT_TRUE(offsets == NULL,
                "range_id_alloc with no matching keys should return NULL");
    EXPECT_TRUE(count == 0,
                "range_id_alloc with no matching keys should set count=0");
cleanup:
    free(offsets);
    cleanup_fixture();
}

int main(void) {
    run_test("init: succeeds without .dat file",      test_init_no_dat_file_succeeds);
    run_test("init: idempotent on double call",       test_init_idempotent);

    run_test("id: insert then search returns offset", test_insert_id_and_search);
    run_test("id: search missing id returns -1",      test_search_missing_id_returns_neg1);
    run_test("id: search uninitialized table is -1",  test_search_uninitialized_table_returns_neg1);
    run_test("id range: basic 5-key range",           test_range_id_basic);
    run_test("id range: outside all keys returns 0",  test_range_id_empty_returns_zero);
    run_test("id range: boundary endpoints included", test_range_id_boundary_inclusive);

    run_test("age: insert then range returns offset", test_insert_age_and_range);
    run_test("age: duplicate age returns 2 offsets",  test_age_range_duplicate_age);

    run_test("cleanup: reinit gives empty index",     test_cleanup_and_reinit);
    run_test("load: init scans .dat and builds index", test_init_loads_dat_file);
    run_test("load: duplicate ages loaded correctly", test_init_loads_duplicate_ages);
    run_test("load: duplicate ids are rejected",      test_init_rejects_duplicate_ids);

    run_test("edge: IDX_MAX_TABLES exceeded -> -1",   test_max_tables_exceeded);
    run_test("edge: range_id_alloc basic",            test_range_id_alloc_basic);
    run_test("edge: range_age_alloc basic",           test_range_age_alloc_basic);
    run_test("edge: range_id_alloc empty -> NULL",    test_range_id_alloc_empty_returns_null);

    if (failures > 0) {
        fprintf(stderr, "\n%d/%d index tests failed.\n", failures, tests_run);
        return 1;
    }

    printf("\nAll %d index tests passed.\n", tests_run);
    return 0;
}
