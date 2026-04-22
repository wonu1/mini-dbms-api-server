#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sched.h>
#endif

#include "../../include/engine_runtime.h"
#include "../../include/index_manager.h"

typedef struct {
    volatile int readers;
    volatile int writer;
    volatile int writers_waiting;
} EngineRwLock;

static EngineRuntimeState g_state;
static EngineRwLock g_lock;

static void engine_runtime_yield(void) {
#ifdef _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
}

static const char *engine_runtime_legacy_schema_dir(void) {
    return "schema";
}

static const char *engine_runtime_active_schema_dir(void) {
    if (g_state.schema_dir[0] != '\0') {
        return g_state.schema_dir;
    }

    if (g_state.initialized) {
        return engine_runtime_default_schema_dir();
    }

    return engine_runtime_legacy_schema_dir();
}

static int path_has_schema_suffix(const char *name) {
    static const char suffix[] = ".schema";
    size_t name_len;
    size_t suffix_len = sizeof(suffix) - 1;

    if (!name) return 0;

    name_len = strlen(name);
    if (name_len <= suffix_len) return 0;

    return strcmp(name + (name_len - suffix_len), suffix) == 0;
}

static void trim_trailing_separators(char *path) {
    size_t len;

    if (!path) return;

    len = strlen(path);
    while (len > 1 &&
           (path[len - 1] == '/' || path[len - 1] == '\\')) {
        path[--len] = '\0';
    }
}

static int copy_table_name_from_schema(const char *filename,
                                       char *table_name,
                                       size_t table_name_size) {
    size_t filename_len;
    size_t table_len;

    if (!filename || !table_name || table_name_size == 0) return 0;
    if (!path_has_schema_suffix(filename)) return 0;

    filename_len = strlen(filename);
    table_len = filename_len - (sizeof(".schema") - 1);
    if (table_len == 0 || table_len >= table_name_size) return 0;

    memcpy(table_name, filename, table_len);
    table_name[table_len] = '\0';
    return 1;
}

static int prepare_table_index(const char *table_name) {
    if (!table_name || table_name[0] == '\0') {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    if (index_init(table_name, IDX_ORDER_DEFAULT, IDX_ORDER_DEFAULT) != 0) {
        return ENGINE_RUNTIME_ERR_SYSTEM;
    }

    return ENGINE_RUNTIME_OK;
}

#ifdef _WIN32
static int prepare_all_tables_from_schema_dir(const char *schema_dir) {
    char pattern[ENGINE_RUNTIME_PATH_MAX];
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    int status = ENGINE_RUNTIME_OK;
    int written;

    if (!schema_dir || schema_dir[0] == '\0') {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    written = snprintf(pattern, sizeof(pattern), "%s%s*.schema",
                       schema_dir,
                       (schema_dir[strlen(schema_dir) - 1] == '/' ||
                        schema_dir[strlen(schema_dir) - 1] == '\\') ? "" : "/");
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return ENGINE_RUNTIME_ERR_SYSTEM;
    }

    do {
        char table_name[64];

        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }

        if (!copy_table_name_from_schema(find_data.cFileName,
                                         table_name,
                                         sizeof(table_name))) {
            continue;
        }

        status = prepare_table_index(table_name);
        if (status != ENGINE_RUNTIME_OK) {
            break;
        }
    } while (FindNextFileA(handle, &find_data) != 0);

    FindClose(handle);
    return status;
}
#else
static int prepare_all_tables_from_schema_dir(const char *schema_dir) {
    DIR *dir;
    struct dirent *entry;
    int status = ENGINE_RUNTIME_OK;

    if (!schema_dir || schema_dir[0] == '\0') {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    dir = opendir(schema_dir);
    if (!dir) {
        return ENGINE_RUNTIME_ERR_SYSTEM;
    }

    while ((entry = readdir(dir)) != NULL) {
        char table_name[64];

        if (!copy_table_name_from_schema(entry->d_name,
                                         table_name,
                                         sizeof(table_name))) {
            continue;
        }

        status = prepare_table_index(table_name);
        if (status != ENGINE_RUNTIME_OK) {
            break;
        }
    }

    closedir(dir);
    return status;
}
#endif

const EngineRuntimeState *engine_runtime_get_state(void) {
    return &g_state;
}

const char *engine_runtime_default_schema_dir(void) {
    return "db_engine/schema";
}

int engine_runtime_build_schema_path(const char *table_name,
                                     char *buffer,
                                     size_t buffer_size) {
    const char *schema_dir = engine_runtime_active_schema_dir();
    const char *separator = "/";
    size_t dir_len;
    int written;

    if (!table_name || table_name[0] == '\0' || !buffer || buffer_size == 0) {
        return 0;
    }

    dir_len = strlen(schema_dir);
    if (dir_len == 0) return 0;

    if (schema_dir[dir_len - 1] == '/' || schema_dir[dir_len - 1] == '\\') {
        separator = "";
    }

    written = snprintf(buffer,
                       buffer_size,
                       "%s%s%s.schema",
                       schema_dir,
                       separator,
                       table_name);
    return written >= 0 && (size_t)written < buffer_size;
}

int engine_runtime_build_data_path(const char *table_name,
                                   char *buffer,
                                   size_t buffer_size) {
    const char *schema_dir = engine_runtime_active_schema_dir();
    char base_dir[ENGINE_RUNTIME_PATH_MAX];
    char *last_separator;
    int written;

    if (!table_name || table_name[0] == '\0' || !buffer || buffer_size == 0) {
        return 0;
    }

    if (strlen(schema_dir) >= sizeof(base_dir)) {
        return 0;
    }

    memcpy(base_dir, schema_dir, strlen(schema_dir) + 1);
    trim_trailing_separators(base_dir);

    last_separator = strrchr(base_dir, '/');
    if (!last_separator) {
        last_separator = strrchr(base_dir, '\\');
    }

    if (!last_separator) {
        written = snprintf(buffer, buffer_size, "data/%s.dat", table_name);
        return written >= 0 && (size_t)written < buffer_size;
    }

    if (last_separator == base_dir) {
        written = snprintf(buffer, buffer_size, "/data/%s.dat", table_name);
        return written >= 0 && (size_t)written < buffer_size;
    }

    *last_separator = '\0';
    written = snprintf(buffer, buffer_size, "%s/data/%s.dat", base_dir, table_name);
    return written >= 0 && (size_t)written < buffer_size;
}

int engine_runtime_lock_shared(void) {
    if (!g_state.initialized || !g_state.lock_ready) {
        return ENGINE_RUNTIME_ERR_STATE;
    }

    for (;;) {
        while (__atomic_load_n(&g_lock.writer, __ATOMIC_ACQUIRE) != 0 ||
               __atomic_load_n(&g_lock.writers_waiting, __ATOMIC_ACQUIRE) != 0) {
            if (!g_state.initialized || !g_state.lock_ready) {
                return ENGINE_RUNTIME_ERR_STATE;
            }
            engine_runtime_yield();
        }

        __atomic_add_fetch(&g_lock.readers, 1, __ATOMIC_ACQUIRE);

        if (__atomic_load_n(&g_lock.writer, __ATOMIC_ACQUIRE) == 0 &&
            __atomic_load_n(&g_lock.writers_waiting, __ATOMIC_ACQUIRE) == 0) {
            return ENGINE_RUNTIME_OK;
        }

        __atomic_sub_fetch(&g_lock.readers, 1, __ATOMIC_RELEASE);
        engine_runtime_yield();
    }
}

int engine_runtime_lock_exclusive(void) {
    if (!g_state.initialized || !g_state.lock_ready) {
        return ENGINE_RUNTIME_ERR_STATE;
    }

    __atomic_add_fetch(&g_lock.writers_waiting, 1, __ATOMIC_ACQUIRE);

    for (;;) {
        int expected = 0;

        if (!g_state.initialized || !g_state.lock_ready) {
            __atomic_sub_fetch(&g_lock.writers_waiting, 1, __ATOMIC_RELEASE);
            return ENGINE_RUNTIME_ERR_STATE;
        }

        if (__atomic_compare_exchange_n(&g_lock.writer,
                                        &expected,
                                        1,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            __atomic_sub_fetch(&g_lock.writers_waiting, 1, __ATOMIC_RELEASE);

            while (__atomic_load_n(&g_lock.readers, __ATOMIC_ACQUIRE) != 0) {
                engine_runtime_yield();
            }

            return ENGINE_RUNTIME_OK;
        }

        engine_runtime_yield();
    }
}

void engine_runtime_unlock_shared(void) {
    if (__atomic_load_n(&g_lock.readers, __ATOMIC_ACQUIRE) > 0) {
        __atomic_sub_fetch(&g_lock.readers, 1, __ATOMIC_RELEASE);
    }
}

void engine_runtime_unlock_exclusive(void) {
    __atomic_store_n(&g_lock.writer, 0, __ATOMIC_RELEASE);
}

int engine_runtime_set_schema_dir(const char *schema_dir) {
    size_t len;

    if (!schema_dir || schema_dir[0] == '\0') {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    if (g_state.initialized && g_state.prepared) {
        return ENGINE_RUNTIME_ERR_STATE;
    }

    len = strlen(schema_dir);
    if (len >= sizeof(g_state.schema_dir)) {
        return ENGINE_RUNTIME_ERR_INVALID_ARG;
    }

    memcpy(g_state.schema_dir, schema_dir, len + 1);
    g_state.prepared = 0;
    return ENGINE_RUNTIME_OK;
}

int engine_runtime_init(void) {
    char configured_schema_dir[ENGINE_RUNTIME_SCHEMA_DIR_MAX];

    if (g_state.initialized) {
        return ENGINE_RUNTIME_OK;
    }

    configured_schema_dir[0] = '\0';
    if (g_state.schema_dir[0] != '\0') {
        memcpy(configured_schema_dir,
               g_state.schema_dir,
               sizeof(configured_schema_dir));
    }

    memset(&g_state, 0, sizeof(g_state));
    memset(&g_lock, 0, sizeof(g_lock));

    if (configured_schema_dir[0] != '\0') {
        memcpy(g_state.schema_dir,
               configured_schema_dir,
               sizeof(g_state.schema_dir));
    } else {
        strncpy(g_state.schema_dir,
                engine_runtime_default_schema_dir(),
                sizeof(g_state.schema_dir) - 1);
    }

    g_state.initialized = 1;
    g_state.lock_ready = 1;
    return ENGINE_RUNTIME_OK;
}

int engine_runtime_prepare_all(void) {
    int status;

    if (!g_state.initialized || !g_state.lock_ready) {
        return ENGINE_RUNTIME_ERR_STATE;
    }

    status = engine_runtime_lock_exclusive();
    if (status != ENGINE_RUNTIME_OK) {
        return status;
    }

    if (g_state.prepared) {
        engine_runtime_unlock_exclusive();
        return ENGINE_RUNTIME_OK;
    }

    index_cleanup();
    status = prepare_all_tables_from_schema_dir(engine_runtime_active_schema_dir());
    if (status == ENGINE_RUNTIME_OK) {
        g_state.prepared = 1;
    } else {
        index_cleanup();
        g_state.prepared = 0;
    }

    engine_runtime_unlock_exclusive();
    return status;
}

void engine_runtime_shutdown(void) {
    if (g_state.lock_ready) {
        (void)engine_runtime_lock_exclusive();
    }

    index_cleanup();
    memset(&g_lock, 0, sizeof(g_lock));
    memset(&g_state, 0, sizeof(g_state));
}
