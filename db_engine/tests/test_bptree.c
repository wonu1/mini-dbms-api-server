/*
 * B+Tree 구현을 직접 검증하는 테스트다.
 * 생성/삽입/탐색/범위 조회와 중복 키, 대량 삽입까지 점검한다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/bptree.h"

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

/* 생성/해제 */

static void test_create_nonnull(void) {
    BPTree *tree = bptree_create(4);
    EXPECT_TRUE(tree != NULL, "bptree_create should return non-null");
cleanup:
    bptree_destroy(tree);
}

static void test_create_order_min_correction(void) {
    BPTree *tree = bptree_create(1);
    EXPECT_TRUE(tree != NULL,
                "bptree_create(1) should succeed after correction to order=3");
cleanup:
    bptree_destroy(tree);
}

static void test_create_order_zero_correction(void) {
    BPTree *tree = bptree_create(0);
    EXPECT_TRUE(tree != NULL,
                "bptree_create(0) should succeed after correction to order=3");
cleanup:
    bptree_destroy(tree);
}

static void test_destroy_null_safe(void) {
    bptree_destroy(NULL);
    EXPECT_TRUE(1, "bptree_destroy(NULL) must not crash");
cleanup:
    ;
}

/* 삽입/탐색 */

static void test_insert_returns_zero(void) {
    BPTree *tree = bptree_create(4);
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    EXPECT_TRUE(bptree_insert(tree, 42, 100L) == 0,
                "insert should return 0 on success");
cleanup:
    bptree_destroy(tree);
}

static void test_insert_null_tree_fails(void) {
    int result = bptree_insert(NULL, 42, 100L);
    EXPECT_TRUE(result != 0,
                "insert on NULL tree should return non-zero error");
cleanup:
    ;
}

static void test_search_returns_inserted_offset(void) {
    BPTree *tree = bptree_create(4);
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    EXPECT_TRUE(bptree_insert(tree, 7, 200L) == 0,
                "insert should succeed");
    EXPECT_TRUE(bptree_search(tree, 7) == 200L,
                "search should return the inserted offset");
cleanup:
    bptree_destroy(tree);
}

static void test_search_missing_key_returns_neg1(void) {
    BPTree *tree = bptree_create(4);
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    EXPECT_TRUE(bptree_insert(tree, 5, 100L) == 0, "insert should succeed");
    EXPECT_TRUE(bptree_search(tree, 99) == -1,
                "search for absent key should return -1");
cleanup:
    bptree_destroy(tree);
}

static void test_search_empty_tree_returns_neg1(void) {
    BPTree *tree = bptree_create(4);
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    EXPECT_TRUE(bptree_search(tree, 1) == -1,
                "search on empty tree should return -1");
cleanup:
    bptree_destroy(tree);
}

static void test_search_null_tree_returns_neg1(void) {
    EXPECT_TRUE(bptree_search(NULL, 1) == -1,
                "search on NULL tree should return -1");
cleanup:
    ;
}

/* 범위 조회 */

static void test_range_empty_tree_returns_zero(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    count = bptree_range(tree, 1, 5, offsets, 10);
    EXPECT_TRUE(count == 0, "range on empty tree should return 0");
cleanup:
    bptree_destroy(tree);
}

static void test_range_null_tree_returns_zero(void) {
    long offsets[10];
    int  count = bptree_range(NULL, 1, 5, offsets, 10);
    EXPECT_TRUE(count == 0, "range on NULL tree should return 0");
cleanup:
    ;
}

static void test_range_from_gt_to_returns_zero(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    bptree_insert(tree, 3, 100L);
    count = bptree_range(tree, 5, 1, offsets, 10);
    EXPECT_TRUE(count == 0, "range where from > to should return 0");
cleanup:
    bptree_destroy(tree);
}

static void test_range_basic_count(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    i;
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 5; i++)
        bptree_insert(tree, i, (long)(i * 100));
    count = bptree_range(tree, 1, 5, offsets, 10);
    EXPECT_TRUE(count == 5, "range [1,5] with 5 inserted keys should return 5");
cleanup:
    bptree_destroy(tree);
}

static void test_range_boundary_inclusive(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    bptree_insert(tree, 1, 100L);
    bptree_insert(tree, 3, 300L);
    bptree_insert(tree, 5, 500L);
    count = bptree_range(tree, 1, 5, offsets, 10);
    EXPECT_TRUE(count == 3, "range [1,5] should include both boundary keys");
cleanup:
    bptree_destroy(tree);
}

static void test_range_excludes_outside_keys(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    i;
    int    count;
    int    found3 = 0;
    int    found4 = 0;
    int    found5 = 0;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 7; i++)
        bptree_insert(tree, i, (long)(i * 100));
    count = bptree_range(tree, 3, 5, offsets, 10);
    EXPECT_TRUE(count == 3, "range [3,5] should return exactly 3 keys");
    for (i = 0; i < count; i++) {
        if (offsets[i] == 300L) found3 = 1;
        if (offsets[i] == 400L) found4 = 1;
        if (offsets[i] == 500L) found5 = 1;
    }
    EXPECT_TRUE(found3 && found4 && found5,
                "range [3,5] should contain offsets 300, 400, 500");
cleanup:
    bptree_destroy(tree);
}

static void test_range_single_key(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    bptree_insert(tree, 10, 999L);
    bptree_insert(tree, 20, 888L);
    count = bptree_range(tree, 10, 10, offsets, 10);
    EXPECT_TRUE(count == 1, "range [x,x] should return exactly 1 result");
    EXPECT_TRUE(offsets[0] == 999L,
                "single-key range should return the correct offset");
cleanup:
    bptree_destroy(tree);
}

static void test_range_no_results(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    bptree_insert(tree, 10, 100L);
    bptree_insert(tree, 20, 200L);
    count = bptree_range(tree, 50, 100, offsets, 10);
    EXPECT_TRUE(count == 0, "range outside all keys should return 0");
cleanup:
    bptree_destroy(tree);
}

/* 중복 키와 대량 삽입 */

static void test_duplicate_key_all_offsets_in_range(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    bptree_insert(tree, 25, 100L);
    bptree_insert(tree, 25, 200L);
    count = bptree_range(tree, 25, 25, offsets, 10);
    EXPECT_TRUE(count == 2,
                "duplicate key should yield 2 offsets in range result");
cleanup:
    bptree_destroy(tree);
}

static void test_duplicate_key_search_returns_valid(void) {
    BPTree *tree = bptree_create(4);
    long   result;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    bptree_insert(tree, 30, 500L);
    bptree_insert(tree, 30, 600L);
    result = bptree_search(tree, 30);
    EXPECT_TRUE(result >= 0,
                "search on duplicate key should return a valid (>=0) offset");
cleanup:
    bptree_destroy(tree);
}

static void test_sequential_insert_all_searchable(void) {
    BPTree *tree = bptree_create(4);
    int    i;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 100; i++)
        bptree_insert(tree, i, (long)(i * 10));
    for (i = 1; i <= 100; i++) {
        EXPECT_TRUE(bptree_search(tree, i) == (long)(i * 10),
                    "sequentially inserted key should be searchable");
    }
cleanup:
    bptree_destroy(tree);
}

static void test_reverse_insert_all_searchable(void) {
    BPTree *tree = bptree_create(4);
    int    i;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 100; i >= 1; i--)
        bptree_insert(tree, i, (long)(i * 10));
    for (i = 1; i <= 100; i++) {
        EXPECT_TRUE(bptree_search(tree, i) == (long)(i * 10),
                    "reverse-inserted key should be searchable");
    }
cleanup:
    bptree_destroy(tree);
}

static void test_large_bulk_insert_small_order(void) {
    BPTree *tree = bptree_create(3);
    int    i;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 50; i++)
        bptree_insert(tree, i, (long)(i * 100));
    for (i = 1; i <= 50; i++) {
        EXPECT_TRUE(bptree_search(tree, i) == (long)(i * 100),
                    "key should remain searchable after many leaf/root splits");
    }
cleanup:
    bptree_destroy(tree);
}

/* range_alloc과 경계 조건 */

static void test_range_alloc_correct_count(void) {
    BPTree *tree = bptree_create(4);
    int    i;
    int    count = 0;
    long  *offsets;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 10; i++)
        bptree_insert(tree, i, (long)(i * 50));
    offsets = bptree_range_alloc(tree, 3, 7, &count);
    EXPECT_TRUE(count == 5,
                "range_alloc [3,7] over keys 1-10 should return count=5");
    EXPECT_TRUE(offsets != NULL,
                "range_alloc should return non-NULL for non-empty result");
cleanup:
    free(offsets);
    bptree_destroy(tree);
}

static void test_range_alloc_null_tree_returns_null(void) {
    int   count  = 99;
    long *result = bptree_range_alloc(NULL, 1, 5, &count);
    EXPECT_TRUE(result == NULL,
                "range_alloc on NULL tree should return NULL");
    EXPECT_TRUE(count == 0,
                "range_alloc on NULL tree should set count to 0");
cleanup:
    free(result);
}

static void test_range_respects_max_count(void) {
    long   offsets[3];
    BPTree *tree = bptree_create(4);
    int    i;
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 10; i++)
        bptree_insert(tree, i, (long)(i * 100));
    count = bptree_range(tree, 1, 10, offsets, 3);
    EXPECT_TRUE(count <= 3,
                "range should not exceed the supplied max_count");
cleanup:
    bptree_destroy(tree);
}

static void test_range_to_is_exclusive_beyond(void) {
    long   offsets[10];
    BPTree *tree = bptree_create(4);
    int    i;
    int    count;
    EXPECT_TRUE(tree != NULL, "tree creation should succeed");
    for (i = 1; i <= 5; i++)
        bptree_insert(tree, i, (long)(i * 100));
    count = bptree_range(tree, 1, 3, offsets, 10);
    EXPECT_TRUE(count == 3,
                "range [1,3] should not include key 4 or 5");
cleanup:
    bptree_destroy(tree);
}

int main(void) {
    run_test("create: returns non-null tree",       test_create_nonnull);
    run_test("create: order=1 corrected to 3",      test_create_order_min_correction);
    run_test("create: order=0 corrected to 3",      test_create_order_zero_correction);
    run_test("destroy: NULL arg is safe",           test_destroy_null_safe);

    run_test("insert: returns 0 on success",        test_insert_returns_zero);
    run_test("insert: NULL tree returns error",     test_insert_null_tree_fails);
    run_test("search: returns inserted offset",     test_search_returns_inserted_offset);
    run_test("search: returns -1 for missing key",  test_search_missing_key_returns_neg1);
    run_test("search: returns -1 on empty tree",    test_search_empty_tree_returns_neg1);
    run_test("search: returns -1 on NULL tree",     test_search_null_tree_returns_neg1);

    run_test("range: returns 0 on empty tree",      test_range_empty_tree_returns_zero);
    run_test("range: returns 0 on NULL tree",       test_range_null_tree_returns_zero);
    run_test("range: returns 0 when from > to",     test_range_from_gt_to_returns_zero);
    run_test("range: correct count for 5 keys",     test_range_basic_count);
    run_test("range: boundary endpoints included",  test_range_boundary_inclusive);
    run_test("range: keys outside bounds excluded", test_range_excludes_outside_keys);
    run_test("range: [x,x] returns exactly 1 key",  test_range_single_key);
    run_test("range: no results outside all keys",  test_range_no_results);

    run_test("dup key: range returns all offsets",   test_duplicate_key_all_offsets_in_range);
    run_test("dup key: search returns valid offset", test_duplicate_key_search_returns_valid);
    run_test("bulk: sequential insert all searchable", test_sequential_insert_all_searchable);
    run_test("bulk: reverse insert all searchable",    test_reverse_insert_all_searchable);
    run_test("split: small order 50-key bulk insert",  test_large_bulk_insert_small_order);

    run_test("range_alloc: correct count",          test_range_alloc_correct_count);
    run_test("range_alloc: NULL tree returns NULL", test_range_alloc_null_tree_returns_null);
    run_test("range: respects max_count limit",     test_range_respects_max_count);
    run_test("range: to boundary is not exceeded",  test_range_to_is_exclusive_beyond);

    if (failures > 0) {
        fprintf(stderr, "\n%d/%d bptree tests failed.\n", failures, tests_run);
        return 1;
    }

    printf("\nAll %d bptree tests passed.\n", tests_run);
    return 0;
}
