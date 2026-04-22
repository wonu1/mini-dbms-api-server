#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/bptree.h"

/*
 * id, age 같은 정수 키를 파일 오프셋에 연결하는 간단한 B+Tree 구현이다.
 * 같은 키에 여러 row가 매달릴 수 있어서 leaf 값은 단일 offset이 아니라
 * offset 목록으로 관리한다.
 */

/* leaf에서 같은 키에 대응되는 offset 목록 */
typedef struct BPValueList {
    long *offsets;
    int   count;
    int   capacity;
} BPValueList;

/*
 * leaf 노드는 values[]를 들고,
 * internal 노드는 children[]을 든다.
 * leaf끼리는 next/prev로 연결해서 range scan을 빠르게 처리한다.
 */
typedef struct BPNode {
    int is_leaf;
    int key_count;

    int *keys;
    BPValueList **values;
    struct BPNode **children;

    struct BPNode *next;
    struct BPNode *prev;
} BPNode;

/* 하위 노드 분할 결과를 상위 호출자에게 올려주기 위한 구조체 */
typedef struct {
    int     did_split;
    int     promoted_key;
    BPNode *right;
} BPSplitResult;

/* range 조회 결과를 동적으로 쌓아 두는 버퍼 */
typedef struct {
    long *offsets;
    int   count;
    int   capacity;
} BPRangeBuffer;

struct BPTree {
    int     order;
    int     height;
    BPNode *root;
};

static BPValueList *valuelist_create(long offset) {
    BPValueList *list = (BPValueList *)calloc(1, sizeof(BPValueList));
    if (!list) return NULL;

    list->capacity = 4;
    list->offsets = (long *)calloc((size_t)list->capacity, sizeof(long));
    if (!list->offsets) {
        free(list);
        return NULL;
    }

    list->offsets[0] = offset;
    list->count = 1;
    return list;
}

static void valuelist_destroy(BPValueList *list) {
    if (!list) return;
    free(list->offsets);
    free(list);
}

/* 같은 키에 대한 offset을 정렬된 상태로 넣는다. */
static int valuelist_insert_sorted(BPValueList *list, long offset) {
    int insert_at = 0;

    if (!list) return -1;

    while (insert_at < list->count && list->offsets[insert_at] <= offset)
        insert_at++;

    if (list->count == list->capacity) {
        int new_capacity = list->capacity * 2;
        long *grown = (long *)realloc(list->offsets,
                                      (size_t)new_capacity * sizeof(long));
        if (!grown) return -1;
        list->offsets = grown;
        list->capacity = new_capacity;
    }

    for (int i = list->count; i > insert_at; i--)
        list->offsets[i] = list->offsets[i - 1];

    list->offsets[insert_at] = offset;
    list->count++;
    return 0;
}

/* 롤백에 쓰기 위해 offset 하나만 제거한다. */
static int valuelist_remove_one(BPValueList *list, long offset) {
    int remove_at = -1;

    if (!list) return -1;

    for (int i = 0; i < list->count; i++) {
        if (list->offsets[i] == offset) {
            remove_at = i;
            break;
        }
    }

    if (remove_at < 0) return -1;

    for (int i = remove_at; i + 1 < list->count; i++)
        list->offsets[i] = list->offsets[i + 1];

    list->count--;
    return 0;
}

static BPNode *bpnode_create(int order, int is_leaf) {
    BPNode *node = (BPNode *)calloc(1, sizeof(BPNode));
    if (!node) return NULL;

    node->is_leaf = is_leaf;
    node->keys = (int *)calloc((size_t)order, sizeof(int));
    if (!node->keys) {
        free(node);
        return NULL;
    }

    if (is_leaf) {
        node->values = (BPValueList **)calloc((size_t)order,
                                              sizeof(BPValueList *));
        if (!node->values) {
            free(node->keys);
            free(node);
            return NULL;
        }
    } else {
        node->children = (BPNode **)calloc((size_t)(order + 1),
                                           sizeof(BPNode *));
        if (!node->children) {
            free(node->keys);
            free(node);
            return NULL;
        }
    }

    return node;
}

static void bpnode_destroy(BPNode *node) {
    if (!node) return;

    if (node->is_leaf) {
        for (int i = 0; i < node->key_count; i++)
            valuelist_destroy(node->values[i]);
        free(node->values);
    } else {
        for (int i = 0; i <= node->key_count; i++)
            bpnode_destroy(node->children[i]);
        free(node->children);
    }

    free(node->keys);
    free(node);
}

static int max_keys(const BPTree *tree) {
    return tree->order - 1;
}

/* internal node에서 다음으로 내려갈 child index를 찾는다. */
static int internal_child_index(const BPNode *node, int key) {
    int idx = 0;
    while (idx < node->key_count && key >= node->keys[idx])
        idx++;
    return idx;
}

/* leaf 안에서 key 이상이 처음 나타나는 위치를 찾는다. */
static int leaf_lower_bound(const BPNode *leaf, int key) {
    int lo = 0;
    int hi = leaf->key_count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (leaf->keys[mid] < key)
            lo = mid + 1;
        else
            hi = mid;
    }

    return lo;
}

/* 가득 찬 leaf를 오른쪽으로 나누고, 부모에 올릴 separator를 준비한다. */
static void split_leaf_into_right(BPNode *leaf,
                                  BPNode *right,
                                  BPSplitResult *result) {
    int old_count = leaf->key_count;
    int split_at = old_count / 2;

    for (int i = split_at; i < old_count; i++) {
        int right_idx = right->key_count;
        right->keys[right_idx] = leaf->keys[i];
        right->values[right_idx] = leaf->values[i];
        right->key_count++;
        leaf->values[i] = NULL;
    }

    leaf->key_count = split_at;

    right->next = leaf->next;
    if (right->next) right->next->prev = right;
    right->prev = leaf;
    leaf->next = right;

    result->did_split = 1;
    result->promoted_key = right->keys[0];
    result->right = right;
}

/* 가득 찬 internal node를 오른쪽으로 나누고 가운데 key를 올린다. */
static void split_internal_into_right(BPNode *node,
                                      BPNode *right,
                                      BPSplitResult *result) {
    int old_count = node->key_count;
    int split_at = old_count / 2;
    int promoted_key = node->keys[split_at];
    int right_key_idx = 0;
    int right_child_idx = 0;

    for (int i = split_at + 1; i < old_count; i++)
        right->keys[right_key_idx++] = node->keys[i];
    right->key_count = right_key_idx;

    for (int i = split_at + 1; i <= old_count; i++) {
        right->children[right_child_idx++] = node->children[i];
        node->children[i] = NULL;
    }

    node->key_count = split_at;

    result->did_split = 1;
    result->promoted_key = promoted_key;
    result->right = right;
}

/* leaf split이 끝난 뒤 새 root 생성에 실패하면 원래 상태로 되돌린다. */
static void rollback_leaf_split(BPNode *left, BPNode *right) {
    int base = left->key_count;

    for (int i = 0; i < right->key_count; i++) {
        left->keys[base + i] = right->keys[i];
        left->values[base + i] = right->values[i];
        right->values[i] = NULL;
    }

    left->key_count += right->key_count;
    left->next = right->next;
    if (left->next) left->next->prev = left;

    bpnode_destroy(right);
}

/* internal split 롤백 */
static void rollback_internal_split(BPNode *left,
                                    int promoted_key,
                                    BPNode *right) {
    int base_keys = left->key_count;
    int base_children = left->key_count + 1;

    left->keys[base_keys] = promoted_key;
    for (int i = 0; i < right->key_count; i++)
        left->keys[base_keys + 1 + i] = right->keys[i];

    for (int i = 0; i <= right->key_count; i++) {
        left->children[base_children + i] = right->children[i];
        right->children[i] = NULL;
    }

    left->key_count = base_keys + 1 + right->key_count;
    bpnode_destroy(right);
}

static void rollback_child_split(BPNode *left_child,
                                 BPSplitResult *child_result) {
    if (!left_child || !child_result || !child_result->did_split ||
        !child_result->right)
        return;

    if (left_child->is_leaf)
        rollback_leaf_split(left_child, child_result->right);
    else
        rollback_internal_split(left_child,
                                child_result->promoted_key,
                                child_result->right);

    child_result->did_split = 0;
    child_result->right = NULL;
}

/* 삽입 도중 최상위 단계에서 실패했을 때 key/offset 하나를 제거한다. */
static int rollback_insert(BPNode *node, int key, long offset) {
    if (!node) return -1;

    if (node->is_leaf) {
        int pos = leaf_lower_bound(node, key);
        BPValueList *list = NULL;

        if (pos >= node->key_count || node->keys[pos] != key)
            return -1;

        list = node->values[pos];
        if (valuelist_remove_one(list, offset) != 0)
            return -1;

        if (list->count > 0)
            return 0;

        valuelist_destroy(list);
        for (int i = pos; i + 1 < node->key_count; i++) {
            node->keys[i] = node->keys[i + 1];
            node->values[i] = node->values[i + 1];
        }

        node->values[node->key_count - 1] = NULL;
        node->key_count--;
        return 0;
    }

    return rollback_insert(node->children[internal_child_index(node, key)],
                           key, offset);
}

/*
 * 실제 삽입의 핵심 재귀 함수다.
 * leaf에 도착하면 값을 넣고, overflow가 생기면 split 결과를 상위로 올린다.
 */
static int bpnode_insert(BPTree *tree, BPNode *node,
                         int key, long offset, BPSplitResult *result) {
    result->did_split = 0;
    result->right = NULL;

    if (node->is_leaf) {
        int pos = leaf_lower_bound(node, key);
        BPNode *right = NULL;

        if (pos < node->key_count && node->keys[pos] == key)
            return valuelist_insert_sorted(node->values[pos], offset);

        BPValueList *list = valuelist_create(offset);
        if (!list) return -1;

        if (node->key_count == max_keys(tree)) {
            right = bpnode_create(tree->order, 1);
            if (!right) {
                valuelist_destroy(list);
                return -1;
            }
        }

        for (int i = node->key_count; i > pos; i--) {
            node->keys[i] = node->keys[i - 1];
            node->values[i] = node->values[i - 1];
        }

        node->keys[pos] = key;
        node->values[pos] = list;
        node->key_count++;

        if (!right)
            return 0;

        split_leaf_into_right(node, right, result);
        return 0;
    }

    int child_idx = internal_child_index(node, key);
    BPNode *right = NULL;
    BPSplitResult child_result = {0, 0, NULL};

    if (bpnode_insert(tree, node->children[child_idx],
                      key, offset, &child_result) != 0)
        return -1;

    if (!child_result.did_split)
        return 0;

    if (node->key_count == max_keys(tree)) {
        right = bpnode_create(tree->order, 0);
        if (!right) {
            rollback_child_split(node->children[child_idx], &child_result);
            rollback_insert(node->children[child_idx], key, offset);
            return -1;
        }
    }

    for (int i = node->key_count; i > child_idx; i--)
        node->keys[i] = node->keys[i - 1];

    for (int i = node->key_count + 1; i > child_idx + 1; i--)
        node->children[i] = node->children[i - 1];

    node->keys[child_idx] = child_result.promoted_key;
    node->children[child_idx + 1] = child_result.right;
    node->key_count++;

    if (!right)
        return 0;

    split_internal_into_right(node, right, result);
    return 0;
}

static int range_buffer_push(BPRangeBuffer *buffer, long offset) {
    if (!buffer) return -1;

    if (buffer->count == buffer->capacity) {
        int new_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
        long *grown = (long *)realloc(buffer->offsets,
                                      (size_t)new_capacity * sizeof(long));
        if (!grown) return -1;
        buffer->offsets = grown;
        buffer->capacity = new_capacity;
    }

    buffer->offsets[buffer->count++] = offset;
    return 0;
}

/* root에서 시작해 key가 있어야 할 leaf까지 내려간다. */
static BPNode *find_leaf(const BPTree *tree, int key) {
    BPNode *node = tree ? tree->root : NULL;

    while (node) {
        if (node->is_leaf)
            return node;
        node = node->children[internal_child_index(node, key)];
    }

    return NULL;
}

BPTree *bptree_create(int order) {
    if (order < 3) order = 3;

    BPTree *tree = (BPTree *)calloc(1, sizeof(BPTree));
    if (!tree) return NULL;

    tree->order = order;
    tree->root = bpnode_create(order, 1);
    if (!tree->root) {
        free(tree);
        return NULL;
    }

    tree->height = 1;
    return tree;
}

void bptree_destroy(BPTree *tree) {
    if (!tree) return;
    bpnode_destroy(tree->root);
    free(tree);
}

/*
 * 삽입이 루트까지 전파되면 새 root를 만든다.
 * 그 과정에서 메모리 할당 실패가 나면 직전 상태로 되돌리도록 했다.
 */
int bptree_insert(BPTree *tree, int key, long value) {
    BPSplitResult result = {0, 0, NULL};

    if (!tree || !tree->root) return -1;

    if (bpnode_insert(tree, tree->root, key, value, &result) != 0)
        return -1;

    if (result.did_split) {
        BPNode *new_root = bpnode_create(tree->order, 0);
        if (!new_root) {
            rollback_child_split(tree->root, &result);
            rollback_insert(tree->root, key, value);
            return -1;
        }

        new_root->keys[0] = result.promoted_key;
        new_root->children[0] = tree->root;
        new_root->children[1] = result.right;
        new_root->key_count = 1;

        tree->root = new_root;
        tree->height++;
    }

    return 0;
}

long bptree_search(BPTree *tree, int key) {
    BPNode *leaf = NULL;
    int pos = 0;

    if (!tree || !tree->root) return -1;

    leaf = find_leaf(tree, key);
    if (!leaf) return -1;

    pos = leaf_lower_bound(leaf, key);
    if (pos >= leaf->key_count || leaf->keys[pos] != key)
        return -1;

    return leaf->values[pos]->offsets[0];
}

/*
 * [from, to] 범위에 포함되는 모든 offset을 새 배열에 담아 돌려준다.
 * leaf 연결 리스트를 따라가며 범위 결과를 모은다.
 */
long *bptree_range_alloc(BPTree *tree, int from, int to, int *out_count) {
    BPNode *leaf = NULL;
    int pos = 0;
    BPRangeBuffer buffer = {0, 0, 0};

    if (out_count) *out_count = 0;

    if (!tree || !tree->root || !out_count) return NULL;
    if (from > to) return NULL;

    leaf = find_leaf(tree, from);
    if (!leaf) return NULL;

    pos = leaf_lower_bound(leaf, from);

    while (leaf) {
        while (pos < leaf->key_count) {
            int key = leaf->keys[pos];
            BPValueList *list = leaf->values[pos];

            if (key > to) {
                *out_count = buffer.count;
                if (buffer.count == 0) {
                    free(buffer.offsets);
                    return NULL;
                }
                return buffer.offsets;
            }

            if (key >= from) {
                for (int i = 0; i < list->count; i++) {
                    if (range_buffer_push(&buffer, list->offsets[i]) != 0) {
                        free(buffer.offsets);
                        *out_count = 0;
                        return NULL;
                    }
                }
            }
            pos++;
        }

        leaf = leaf->next;
        pos = 0;
    }

    *out_count = buffer.count;
    if (buffer.count == 0) {
        free(buffer.offsets);
        return NULL;
    }

    return buffer.offsets;
}

int bptree_range(BPTree *tree, int from, int to, long *out, int max_count) {
    int count = 0;
    int copied = 0;
    long *all_offsets = NULL;

    if (!tree || !tree->root || !out || max_count <= 0) return 0;
    if (from > to) return 0;

    all_offsets = bptree_range_alloc(tree, from, to, &count);
    if (!all_offsets || count <= 0) return 0;

    copied = (count < max_count) ? count : max_count;
    memcpy(out, all_offsets, (size_t)copied * sizeof(long));
    free(all_offsets);
    return copied;
}
