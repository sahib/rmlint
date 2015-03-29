#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <linux/limits.h>

#include "swap-table.h"

#if HAVE_SQLITE3 

#define SET_ERROR(...) \
    g_set_error(error, RM_ERROR_QUARK, 0, __VA_ARGS__); \


/////////////////////
//  GENERAL TYPES  //
/////////////////////

enum { STMT_CREATE_ATTR, STMT_INSERT_DATA, STMT_SELECT_DATA, N_STMTS };

static const char *STATEMENTS[] =
    {[STMT_CREATE_ATTR] = "CREATE TABLE %s_vec (%s BLOB NOT NULL);",
     [STMT_INSERT_DATA] = "INSERT INTO %s_vec VALUES(?); -- %s",
     [STMT_SELECT_DATA] = "SELECT %s FROM %s_vec WHERE rowid = ?;",
     [N_STMTS] = NULL};

typedef struct RmSwapAttr {
    int id, count;
    const char *name;
    sqlite3_stmt *stmts[N_STMTS];
} RmSwapAttr;


/////////////////////////
//  UTILITY FUNCTIONS  //
/////////////////////////

static RmSwapAttr *rm_swap_attr_create(sqlite3 *handle, int id,
                                       const char *name, GError **error) {
    g_assert(name);

    RmSwapAttr *self = g_malloc0(sizeof(RmSwapAttr));
    self->name = name;
    self->id = id;

    for(int idx = 0; idx < N_STMTS; ++idx) {
        char *dynamic_sql = sqlite3_mprintf(STATEMENTS[idx], name, name);

        if(sqlite3_prepare_v2(handle, dynamic_sql, -1, &self->stmts[idx],
                              NULL) != SQLITE_OK) {
            SET_ERROR("Unable to prepare statement");
        }

        if(idx == STMT_CREATE_ATTR) {
            sqlite3_stmt *stmt = self->stmts[idx];
            if(sqlite3_step(stmt) != SQLITE_DONE) {
                SET_ERROR("Unable to create attr table");
            }

            if(sqlite3_reset(stmt) != SQLITE_OK) {
                SET_ERROR("Unable to reset attr attr table stmt");
            }
        }

        if(dynamic_sql != NULL) {
            sqlite3_free(dynamic_sql);
        }
    }

    return self;
}

static void rm_swap_attr_destroy(RmSwapAttr *self, RmSwapTable *table) {
    g_assert(self);
    g_assert(table);

    for(int idx = 0; idx < N_STMTS; ++idx) {
        sqlite3_stmt *stmt = self->stmts[idx];
        sqlite3_finalize(stmt);
    }

    g_free(self);
}

static void rm_swap_table_clean_stmt(RmSwapTable *self, sqlite3_stmt *stmt,
                                     GError **error) {
    g_assert(self);
    g_assert(stmt);

    int r = 0;
    if((r = sqlite3_errcode(self->cache)) != SQLITE_DONE) {
        g_printerr(">>> %d\n", r);
        SET_ERROR("stmt failed: %s", sqlite3_errmsg(self->cache));
    }

    if(sqlite3_reset(stmt) != SQLITE_OK) {
        SET_ERROR("Unable to reset prepared statement");
    }

    if(sqlite3_clear_bindings(stmt) != SQLITE_OK) {
        SET_ERROR("Unable to clear prepared statement");
    }
}

static void rm_swap_table_create_cachedir(GError **error) {
    char *path = g_build_filename(g_get_user_cache_dir(), "rmlint", NULL);

    if(g_mkdir_with_parents(path, 0775) == -1) {
        SET_ERROR("cannot create cache dir %s: %s", path, g_strerror(errno));
    }

    g_free(path);
}

//////////////////////
//  SWAP TABLE API  //
//////////////////////

RmSwapTable *rm_swap_table_open(gboolean in_memory, GError **error) {
    RmSwapTable *self = NULL;

    char *path = NULL;

    if(in_memory) {
        path = g_strdup(":memory:");
    } else {
        char pid[20] = {0};
        g_snprintf(pid, sizeof(pid), "%d", getpid());
        path = g_build_filename(g_get_user_cache_dir(), "rmlint", pid, NULL);

        /* Make sure that path actually exists */
        rm_swap_table_create_cachedir(error);
    }

    /* Might happen if no tmp file could be created */
    if(error && *error) {
        goto cleanup;
    }

    /* We do locking ourselves */
    sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);

    sqlite3 *handle = NULL;
    if(sqlite3_open(path, &handle) != SQLITE_OK) {
        SET_ERROR("Cannot open swap table db");
        goto cleanup;
    }

    /* Finetune sqlite (quite slow without these) */
    sqlite3_extended_result_codes(handle, TRUE);
    sqlite3_exec(handle, "PRAGMA cache_size = 8000;", 0, 0, 0);
    sqlite3_exec(handle, "PRAGMA synchronous = OFF;", 0, 0, 0);
    sqlite3_exec(handle, "PRAGMA journal_mode = MEMORY;", 0, 0, 0);
    sqlite3_enable_shared_cache(TRUE);

    size_t path_len = strlen(path) + 1;
    self = g_malloc(sizeof(RmSwapTable) + path_len);
    self->cache = handle;
    self->attrs = g_ptr_array_new();
    self->transaction_running = FALSE;
    strncpy(self->path, path, path_len);
    g_mutex_init(&self->mtx);

cleanup:
    g_free(path);
    return self;
}

void rm_swap_table_close(RmSwapTable *self, GError **error) {
    g_assert(self);

    g_ptr_array_foreach(self->attrs, (GFunc)rm_swap_attr_destroy, self);
    g_ptr_array_free(self->attrs, TRUE);

    if(sqlite3_close(self->cache) != SQLITE_OK) {
        SET_ERROR("Unable to close swap table db");
    }

    if(unlink(self->path) == -1) {
        SET_ERROR("cannot delete temp cache %s: %s", self->path, g_strerror(errno));
    }

    g_mutex_clear(&self->mtx);
    self->cache = NULL;
    self->attrs = NULL;
    g_free(self);
}

int rm_swap_table_create_attr(RmSwapTable *self, const char *name,
                              GError **error) {
    g_assert(self);

    RmSwapAttr *attribute = NULL;
    g_mutex_lock(&self->mtx); {
        attribute  = rm_swap_attr_create(self->cache, self->attrs->len, name, error);
        g_ptr_array_add(self->attrs, attribute);
    }
    g_mutex_unlock(&self->mtx);

    /* Create RmSwapAttr, return id, add it to table, create table. */
    return (attribute) ? attribute->id : 0;
}

static void rm_swap_table_begin(RmSwapTable *self) {
    g_assert(self);

    if(sqlite3_exec(self->cache, "BEGIN IMMEDIATE;", 0, 0, 0) == SQLITE_OK) {
        self->transaction_running = TRUE;
    }
}

static void rm_swap_table_commit(RmSwapTable *self) {
    g_assert(self);

    if(sqlite3_exec(self->cache, "COMMIT;", 0, 0, 0) == SQLITE_OK) {
        self->transaction_running = FALSE;
    }
}

size_t rm_swap_table_lookup(RmSwapTable *self, int attr, RmOff id, char *buf,
                            size_t buf_size) {
    g_assert(self);

    size_t bytes_written = 0;

    g_mutex_lock(&self->mtx); {
        /* Commit any stalled transactions */
        if(self->transaction_running) {
            rm_swap_table_commit(self);
        }

        RmSwapAttr *attribute = self->attrs->pdata[attr];
        sqlite3_stmt *stmt = attribute->stmts[STMT_SELECT_DATA];

        if(sqlite3_bind_int64(stmt, 1, id) != SQLITE_OK) {
            g_assert_not_reached();
        }

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            bytes_written = MIN((size_t)sqlite3_column_bytes(stmt, 0), buf_size);
            memcpy(buf, sqlite3_column_blob(stmt, 0), bytes_written);
        }

        sqlite3_step(stmt); /* == SQLITE_DONE */

        rm_swap_table_clean_stmt(self, stmt, NULL);
    }
    g_mutex_unlock(&self->mtx);

    return bytes_written;
}

RmOff rm_swap_table_insert(RmSwapTable *self, int attr, char *data,
                         size_t data_len) {
    g_assert(self);

    RmOff id = 0;

    g_mutex_lock(&self->mtx); {
        /* Begin an transaction if none is running yet */
        if(self->transaction_running == FALSE) {
            rm_swap_table_begin(self);
        }

        RmSwapAttr *attribute = self->attrs->pdata[attr];
        sqlite3_stmt *stmt = attribute->stmts[STMT_INSERT_DATA];

        if(sqlite3_bind_text(stmt, 1, data, data_len, NULL) != SQLITE_OK) {
            g_assert_not_reached();
        }

        if(sqlite3_step(stmt) == SQLITE_DONE) {
            id = attribute->count = attribute->count + 1;
        }

        rm_swap_table_clean_stmt(self, stmt, NULL);
    }
    g_mutex_unlock(&self->mtx);

    return id;
}

#else /* HAVE_SQLITE3 */

RmSwapTable *rm_swap_table_open(_U gboolean in_memory, _U GError **error) {
    return NULL;
}

void rm_swap_table_close(_U RmSwapTable *self, _U GError **error) {
    return;
}

int rm_swap_table_create_attr(_U RmSwapTable *self, _U const char *name, _U GError **error) {
    return 0;
}

RmOff rm_swap_table_insert(_U RmSwapTable *self, _U int attr, _U char *data, _U size_t data_len) {
    return 0;
}


size_t rm_swap_table_lookup(_U RmSwapTable *self, _U int attr, _U RmOff id, _U char *buf, _U size_t buf_size) {
    return 0;
}

#endif

//////////////////////
//  UGLY TEST MAIN  //
//////////////////////

// #define _RM_COMPILE_SWAP_TABLE_MAIN
#ifdef _RM_COMPILE_SWAP_TABLE_MAIN

int main(void) {
    GError *error = NULL;
    RmSwapTable *table = rm_swap_table_open(FALSE, &error);

    if(error != NULL) {
        return EXIT_FAILURE;
    }

    g_printerr("%s\n", table->path);

    int PATH_ATTR = rm_swap_table_create_attr(table, "path", &error);

    const int N = 1000000;
    const int PATH_LEN = 80; /* Typical average path len */

    for(int i = 0; i < (N); i++) {
        char buf[PATH_LEN + 1];
        memset(buf, (i % ('~' - '!')) + '!', PATH_LEN);

        buf[PATH_LEN] = 0;

        rm_swap_table_insert(table, PATH_ATTR, buf, sizeof(buf));
    }

    g_printerr("COMMIT DONE;\n");

    for(int i = 0; i < (N); i++) {
        char buf[PATH_MAX];
        rm_swap_table_lookup(table, PATH_ATTR, i + 1, buf, sizeof(buf));
        // g_printerr("%d -> %d -> %s\n", PATH_ATTR, i, buf);
    }

    rm_swap_table_close(table, &error);

    if(error != NULL) {
        g_error_free(error);
    }

    return EXIT_SUCCESS;
}

#endif
