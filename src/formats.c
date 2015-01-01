/*
 *  This file is part of rmlint.
 *
 *  rmlint is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  rmlint is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with rmlint.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <stdlib.h>
#include <string.h>

#include "formats.h"

const char *rm_fmt_progress_to_string(RmFmtProgressState state) {
    static const char *table[] = {
        [RM_PROGRESS_STATE_INIT]       = "Initializing",
        [RM_PROGRESS_STATE_TRAVERSE]   = "Traversing",
        [RM_PROGRESS_STATE_PREPROCESS] = "Preprocessing",
        [RM_PROGRESS_STATE_SHREDDER]   = "Shreddering",
        [RM_PROGRESS_STATE_MERGE]      = "Merging",
        [RM_PROGRESS_STATE_SUMMARY]    = "Finalizing",
        [RM_PROGRESS_STATE_N]          = "Unknown state"
    };

    return table[(state < RM_PROGRESS_STATE_N) ? state : RM_PROGRESS_STATE_N];
}

static void rm_fmt_handler_free(RmFmtHandler *handler) {
    g_free(handler->path);
    g_free(handler);
}

RmFmtTable *rm_fmt_open(RmSession *session) {
    RmFmtTable *self = g_slice_new0(RmFmtTable);

    self->name_to_handler = g_hash_table_new_full(
                                g_str_hash, g_str_equal, NULL, NULL
                            );

    self->path_to_handler = g_hash_table_new_full(
                                g_str_hash, g_str_equal, NULL, NULL
                            );

    self->handler_to_file = g_hash_table_new_full(
                                NULL, NULL, (GDestroyNotify)rm_fmt_handler_free, NULL
                            );

    self->config = g_hash_table_new_full(
                       g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref
                   );

    self->session = session;
    g_rec_mutex_init(&self->state_mtx);

    extern RmFmtHandler *PROGRESS_HANDLER;
    rm_fmt_register(self, PROGRESS_HANDLER);

    extern RmFmtHandler *CSV_HANDLER;
    rm_fmt_register(self, CSV_HANDLER);

    extern RmFmtHandler *PRETTY_HANDLER;
    rm_fmt_register(self, PRETTY_HANDLER);

    extern RmFmtHandler *SH_SCRIPT_HANDLER;
    rm_fmt_register(self, SH_SCRIPT_HANDLER);

    extern RmFmtHandler *SUMMARY_HANDLER;
    rm_fmt_register(self, SUMMARY_HANDLER);

    extern RmFmtHandler *TIMESTAMP_HANDLER;
    rm_fmt_register(self, TIMESTAMP_HANDLER);

    extern RmFmtHandler *JSON_HANDLER;
    rm_fmt_register(self, JSON_HANDLER);

    extern RmFmtHandler *PY_HANDLER;
    rm_fmt_register(self, PY_HANDLER);

    return self;
}

void rm_fmt_register(RmFmtTable *self, RmFmtHandler *handler) {
    g_hash_table_insert(self->name_to_handler, (char *) handler->name, handler);
    g_mutex_init(&handler->print_mtx);
}

#define RM_FMT_FOR_EACH_HANDLER(self)                                             \
    FILE * file = NULL;                                                           \
    RmFmtHandler * handler = NULL;                                                \
                                                                                  \
    GHashTableIter iter;                                                          \
    g_hash_table_iter_init(&iter, self->handler_to_file);                         \
    while(g_hash_table_iter_next(&iter, (gpointer *)&handler, (gpointer *)&file)) \

#define RM_FMT_CALLBACK(func, ...)                               \
    if(func) {                                                   \
        g_mutex_lock(&handler->print_mtx); {                     \
            if(!handler->was_initialized && handler->head) {     \
                if(handler->head) {                              \
                    handler->head(self->session, handler, file); \
                }                                                \
                handler->was_initialized = true;                 \
            }                                                    \
            func(self->session, handler, file, ##__VA_ARGS__);   \
        }                                                        \
        g_mutex_unlock(&handler->print_mtx);                     \
    }                                                            \

bool rm_fmt_add(RmFmtTable *self, const char *handler_name, const char *path) {
    RmFmtHandler *new_handler = g_hash_table_lookup(self->name_to_handler, handler_name);
    if(new_handler == NULL) {
        rm_log_warning_line(_("No such new_handler with this name: %s"), handler_name);
        return false;
    }

    g_return_val_if_fail(path, false);

    size_t path_len = (path) ? strlen(path) : 0;
    FILE *file_handle = NULL;
    bool needs_full_path = false;

    if(strncmp(path, "stdout", path_len) == 0) {
        file_handle = stdout;
    } else if(strncmp(path, "stderr", path_len) == 0) {
        file_handle = stderr;
    } else if(strncmp(path, "stdin", path_len) == 0) {
        /* I bet someone finds a use for this :-) */
        file_handle = stdin;
    } else {
        needs_full_path = true;
        file_handle = fopen(path, "w");
    }

    if(file_handle == NULL) {
        rm_log_warning_line(_("Unable to open file for writing: %s"), path);
        return false;
    }

    /* Make a copy of the handler so we can more than one per handler type.
     * Plus we have to set the handler specific path.
     */
    RmFmtHandler *new_handler_copy = g_malloc0(new_handler->size);
    memcpy(new_handler_copy, new_handler, new_handler->size);
    g_mutex_init(&new_handler->print_mtx);

    if(needs_full_path == false) {
        new_handler_copy->path = g_strdup(path);
    } else {
        new_handler_copy->path = realpath(path, NULL);
    }

    g_hash_table_insert(
        self->handler_to_file, new_handler_copy, file_handle
    );

    g_hash_table_insert(
        self->path_to_handler, new_handler_copy->path, new_handler
    );

    return true;
}

void rm_fmt_close(RmFmtTable *self) {
    RM_FMT_FOR_EACH_HANDLER(self) {
        RM_FMT_CALLBACK(handler->foot);
        fclose(file);
        g_mutex_clear(&handler->print_mtx);
    }

    g_hash_table_unref(self->name_to_handler);
    g_hash_table_unref(self->handler_to_file);
    g_hash_table_unref(self->path_to_handler);
    g_hash_table_unref(self->config);
    g_rec_mutex_clear(&self->state_mtx);
    g_slice_free(RmFmtTable, self);
}

void rm_fmt_write(RmFile *result, RmFmtTable *self) {
    RM_FMT_FOR_EACH_HANDLER(self) {
        RM_FMT_CALLBACK(handler->elem, result);
    }
}

void rm_fmt_lock_state(RmFmtTable *self) {
    g_rec_mutex_lock(&self->state_mtx);
}

void rm_fmt_unlock_state(RmFmtTable *self) {
    g_rec_mutex_unlock(&self->state_mtx);
}

void rm_fmt_set_state(RmFmtTable *self, RmFmtProgressState state) {
    rm_fmt_lock_state(self);
    {
        RM_FMT_FOR_EACH_HANDLER(self) {
            RM_FMT_CALLBACK(handler->prog, state);
        }
    }
    rm_fmt_unlock_state(self);
}

void rm_fmt_set_config_value(RmFmtTable *self, const char *formatter, const char *key, const char *value) {
    GHashTable *key_to_vals = g_hash_table_lookup(self->config, formatter);

    if(key_to_vals == NULL) {
        key_to_vals = g_hash_table_new_full(
                          g_str_hash, g_str_equal, g_free, g_free
                      );
        g_hash_table_insert(self->config, (char *) formatter, key_to_vals);
    }

    g_hash_table_insert(key_to_vals, (char *) key, (char *) value);
}

const char *rm_fmt_get_config_value(RmFmtTable *self, const char *formatter, const char *key) {
    GHashTable *key_to_vals = g_hash_table_lookup(self->config, formatter);

    if(key_to_vals == NULL) {
        return NULL;
    }

    return g_hash_table_lookup(key_to_vals, key);
}

bool rm_fmt_is_a_output(RmFmtTable *self, const char *path) {
    return g_hash_table_contains(self->path_to_handler, path);
}

void rm_fmt_get_pair_iter(RmFmtTable *self, GHashTableIter *iter) {
    g_hash_table_iter_init(iter, self->path_to_handler);
}
