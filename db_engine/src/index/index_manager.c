#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/bptree.h"
#include "../../include/engine_runtime.h"
#include "../../include/index_manager.h"
#include "../../include/interface.h"

typedef struct {
    char    table[64];
    BPTree *tree_id;
    BPTree *tree_age;
    int     initialized;
    int     next_auto_id;
} TableIndex;

static TableIndex g_tables[IDX_MAX_TABLES];
static int        g_count = 0;

static TableIndex *find_entry(const char *table) {
    int i;

    for (i = 0; i < g_count; i++) {
        if (strcmp(g_tables[i].table, table) == 0) return &g_tables[i];
    }

    return NULL;
}

static void clear_entry(TableIndex *entry) {
    if (!entry) return;

    bptree_destroy(entry->tree_id);
    bptree_destroy(entry->tree_age);
    memset(entry, 0, sizeof(*entry));
}

static int col_value(const char *line, int n, char *buf, int buf_size) {
    const char *p = line;
    int col = 0;
    int i = 0;

    while (*p && col < n) {
        if (*p == '|') col++;
        p++;
    }
    if (col < n) return 0;

    while (*p == ' ') p++;

    while (*p && *p != '|' && *p != '\n' && *p != '\r' && i < buf_size - 1) {
        buf[i++] = *p++;
    }

    while (i > 0 && buf[i - 1] == ' ') i--;
    buf[i] = '\0';
    return 1;
}

int index_init(const char *table, int order_id, int order_age) {
    TableIndex *ti;
    int oid;
    int oage;
    char path[ENGINE_RUNTIME_PATH_MAX];
    FILE *fp;
    char line[1024];
    char col_buf[64];
    int inserted = 0;
    int max_id = 0;

    if (!table || g_count >= IDX_MAX_TABLES) return -1;

    ti = find_entry(table);
    if (ti) return 0;

    ti = &g_tables[g_count];
    memset(ti, 0, sizeof(*ti));
    strncpy(ti->table, table, sizeof(ti->table) - 1);

    oid = (order_id > 2) ? order_id : IDX_ORDER_DEFAULT;
    oage = (order_age > 2) ? order_age : IDX_ORDER_DEFAULT;

    ti->tree_id = bptree_create(oid);
    ti->tree_age = bptree_create(oage);
    if (!ti->tree_id || !ti->tree_age) {
        clear_entry(ti);
        return -1;
    }

    if (!engine_runtime_build_data_path(table, path, sizeof(path))) {
        clear_entry(ti);
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        ti->initialized = 1;
        ti->next_auto_id = 1;
        g_count++;
        fprintf(stderr,
                "[index] '%s' initialized (no data file, empty index)\n",
                table);
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        long offset = ftell(fp) - (long)strlen(line);
        int id;
        int age;

        {
            int len = (int)strlen(line);
            while (len > 0 &&
                   (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            if (len == 0) continue;
        }

        if (!col_value(line, 0, col_buf, sizeof(col_buf))) continue;
        id = atoi(col_buf);

        if (bptree_search(ti->tree_id, id) >= 0) {
            fprintf(stderr,
                    "index: duplicate id %d found while loading table '%s'\n",
                    id, table);
            fclose(fp);
            clear_entry(ti);
            return -1;
        }

        if (!col_value(line, 2, col_buf, sizeof(col_buf))) continue;
        age = atoi(col_buf);

        bptree_insert(ti->tree_id, id, offset);
        bptree_insert(ti->tree_age, age, offset);
        if (id > max_id) max_id = id;
        inserted++;
    }

    fclose(fp);

    ti->initialized = 1;
    ti->next_auto_id = max_id + 1;
    if (ti->next_auto_id <= 0) ti->next_auto_id = 1;
    g_count++;

    fprintf(stderr,
            "[index] '%s' initialized - %d rows loaded "
            "(order_id=%d, order_age=%d, next_auto_id=%d)\n",
            table, inserted, oid, oage, ti->next_auto_id);

    return 0;
}

void index_cleanup(void) {
    int i;

    for (i = 0; i < g_count; i++) {
        clear_entry(&g_tables[i]);
    }

    g_count = 0;
}

int index_insert_id(const char *table, int id, long offset) {
    TableIndex *ti = find_entry(table);
    if (!ti) return -1;
    return bptree_insert(ti->tree_id, id, offset);
}

int index_next_auto_id(const char *table, int *out_id) {
    TableIndex *ti = find_entry(table);

    if (!ti || !out_id) return -1;

    *out_id = ti->next_auto_id;
    ti->next_auto_id++;
    return 0;
}

long index_search_id(const char *table, int id) {
    TableIndex *ti = find_entry(table);
    if (!ti) return -1;
    return bptree_search(ti->tree_id, id);
}

long *index_range_id_alloc(const char *table, int from, int to, int *out_count) {
    TableIndex *ti = find_entry(table);

    if (out_count) *out_count = 0;
    if (!ti || !out_count) return NULL;

    return bptree_range_alloc(ti->tree_id, from, to, out_count);
}

int index_range_id(const char *table, int from, int to, long *offsets, int max) {
    int count = 0;
    int copied;
    long *all_offsets;

    if (!offsets || max <= 0) return 0;

    all_offsets = index_range_id_alloc(table, from, to, &count);
    if (!all_offsets || count <= 0) return 0;

    copied = (count < max) ? count : max;
    memcpy(offsets, all_offsets, (size_t)copied * sizeof(long));
    free(all_offsets);
    return copied;
}

int index_insert_age(const char *table, int age, long offset) {
    TableIndex *ti = find_entry(table);
    if (!ti) return -1;
    return bptree_insert(ti->tree_age, age, offset);
}

long *index_range_age_alloc(const char *table, int from, int to, int *out_count) {
    TableIndex *ti = find_entry(table);

    if (out_count) *out_count = 0;
    if (!ti || !out_count) return NULL;

    return bptree_range_alloc(ti->tree_age, from, to, out_count);
}

int index_range_age(const char *table, int from, int to, long *offsets, int max) {
    int count = 0;
    int copied;
    long *all_offsets;

    if (!offsets || max <= 0) return 0;

    all_offsets = index_range_age_alloc(table, from, to, &count);
    if (!all_offsets || count <= 0) return 0;

    copied = (count < max) ? count : max;
    memcpy(offsets, all_offsets, (size_t)copied * sizeof(long));
    free(all_offsets);
    return copied;
}
