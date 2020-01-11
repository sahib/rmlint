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
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "file.h"
#include "formats.h"

/* A group of output files.
 * These are only created when caching to the end of the run is requested.
 * Otherwise, files are directly outputed and not stored in groups.
 */
typedef struct RmFmtGroup {
    GQueue files;
    int index;
} RmFmtGroup;

static RmFmtGroup *rm_fmt_group_new(void) {
    RmFmtGroup *self = g_slice_new(RmFmtGroup);
    g_queue_init(&self->files);
    return self;
}

static void rm_fmt_group_destroy(_UNUSED RmFmtTable *self, RmFmtGroup *group) {
    for(GList *iter = group->files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        rm_file_destroy(file);
    }

    g_queue_clear(&group->files);
    g_slice_free(RmFmtGroup, group);
}

static void rm_fmt_handler_free(RmFmtHandler *handler) {
    g_assert(handler);
    g_free(handler->path);
    g_free(handler);
}

RmFmtTable *rm_fmt_open(RmSession *session) {
    RmFmtTable *self = g_slice_new0(RmFmtTable);

    self->name_to_handler = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

    self->path_to_handler = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

    /* Set of registered handler names */
    self->handler_set = g_hash_table_new(g_str_hash, g_str_equal);

    self->handler_to_file =
        g_hash_table_new_full(NULL, NULL, (GDestroyNotify)rm_fmt_handler_free, NULL);

    self->config = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                         (GDestroyNotify)g_hash_table_unref);

    self->handler_order = g_queue_new();

    self->session = session;
    g_queue_init(&self->groups);
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

    extern RmFmtHandler *FDUPES_HANDLER;
    rm_fmt_register(self, FDUPES_HANDLER);

    extern RmFmtHandler *UNIQUES_HANDLER;
    rm_fmt_register(self, UNIQUES_HANDLER);

    extern RmFmtHandler *NULL_HANDLER;
    rm_fmt_register(self, NULL_HANDLER);

    extern RmFmtHandler *STATS_HANDLER;
    rm_fmt_register(self, STATS_HANDLER);

    extern RmFmtHandler *EQUAL_HANDLER;
    rm_fmt_register(self, EQUAL_HANDLER);

    return self;
}

int rm_fmt_len(RmFmtTable *self) {
    if(self == NULL) {
        return -1;
    } else {
        return g_hash_table_size(self->handler_to_file);
    }
}

bool rm_fmt_is_valid_key(RmFmtTable *self, const char *formatter, const char *key) {
    RmFmtHandler *handler = g_hash_table_lookup(self->name_to_handler, formatter);
    if(handler == NULL) {
        return false;
    }

    for(int i = 0; handler->valid_keys[i]; ++i) {
        if(g_strcmp0(handler->valid_keys[i], key) == 0) {
            return true;
        }
    }

    return false;
}

void rm_fmt_remove_by_name(RmFmtTable *self, char *name) {
    GQueue deleted_iters = G_QUEUE_INIT;
    for(GList *iter = self->handler_order->head; iter; iter = iter->next) {
        RmFmtHandler *handler = iter->data;
        if(!g_str_equal(handler->name, name)) {
            continue;
        }

        g_hash_table_remove(self->handler_to_file, handler);
        g_hash_table_remove(self->path_to_handler, handler->path);
        g_queue_push_tail(&deleted_iters, iter);
    }

    for(GList *iter = deleted_iters.head; iter; iter = iter->next) {
        g_queue_delete_link(self->handler_order, iter);
    }

    // Since we removed all handlers
    g_hash_table_remove(self->handler_set, name);
}

// NOTE: This is the same as g_date_time_format_iso8601.
// It is only copied here because it is only available since
// GLib 2.62. Remove this in a few years (written as of end 2019)
gchar *rm_date_time_format_iso8601(GDateTime *datetime) {
    GString *outstr = NULL;
    gchar *main_date = NULL;
    gint64 offset;

    /* Main date and time. */
    main_date = g_date_time_format(datetime, "%Y-%m-%dT%H:%M:%S");
    outstr = g_string_new(main_date);
    g_free(main_date);

    /* Timezone. Format it as `%:::z` unless the offset is zero, in which case
     * we can simply use `Z`. */
    offset = g_date_time_get_utc_offset(datetime);

    if(offset == 0) {
        g_string_append_c(outstr, 'Z');
    } else {
        gchar *time_zone = g_date_time_format(datetime, "%:::z");
        g_string_append (outstr, time_zone);
        g_free (time_zone);
    }

    return g_string_free(outstr, FALSE);
}


void rm_fmt_clear(RmFmtTable *self) {
    if(rm_fmt_len(self) <= 0) {
        return;
    }

    g_hash_table_remove_all(self->handler_set);
    g_hash_table_remove_all(self->handler_to_file);
    g_hash_table_remove_all(self->path_to_handler);
    g_hash_table_remove_all(self->config);
    g_queue_clear(self->handler_order);
}

void rm_fmt_backup_old_result_file(RmFmtTable *self, const char *old_path) {
    if(self->first_backup_timestamp == NULL) {
        self->first_backup_timestamp = g_date_time_new_now_utc();
    }

    char *new_path = NULL;
    char *timestamp = rm_date_time_format_iso8601(self->first_backup_timestamp);

    // Split the extension, if possible and place it before the timestamp suffix.
    char *extension = g_utf8_strrchr(old_path, -1, '.');
    if(extension != NULL) {
        char *old_path_prefix = g_strndup(old_path, extension - old_path);
        new_path = g_strdup_printf("%s.%s.%s", old_path_prefix, timestamp, extension + 1);
        g_free(old_path_prefix);
    } else {
        new_path = g_strdup_printf("%s.%s", old_path, timestamp);
    }


    rm_log_debug_line(_("Old result `%s` already exists."), old_path);
    rm_log_debug_line(_("Moving old file to `%s`. Leave out --backup to disable this."), new_path);

    if(rename(old_path, new_path) < 0) {
        rm_log_perror(_("failed to rename old result file"));
    }

    g_free(new_path);
    g_free(timestamp);
}

void rm_fmt_register(RmFmtTable *self, RmFmtHandler *handler) {
    g_hash_table_insert(self->name_to_handler, (char *)handler->name, handler);
    g_mutex_init(&handler->print_mtx);
}

#define RM_FMT_FOR_EACH_HANDLER_BEGIN(self)                                 \
    for(GList *iter = self->handler_order->head; iter; iter = iter->next) { \
        RmFmtHandler *handler = iter->data;                                 \
        FILE *file = g_hash_table_lookup(self->handler_to_file, handler);

#define RM_FMT_FOR_EACH_HANDLER_END }

#define RM_FMT_CALLBACK(func, ...)                               \
    if(func) {                                                   \
        g_mutex_lock(&handler->print_mtx);                       \
        {                                                        \
            if(!handler->was_initialized && handler->head) {     \
                if(handler->head) {                              \
                    handler->head(self->session, handler, file); \
                }                                                \
                handler->was_initialized = true;                 \
            }                                                    \
            func(self->session, handler, file, ##__VA_ARGS__);   \
        }                                                        \
        g_mutex_unlock(&handler->print_mtx);                     \
    }

bool rm_fmt_add(RmFmtTable *self, const char *handler_name, const char *path) {
    RmFmtHandler *new_handler = g_hash_table_lookup(self->name_to_handler, handler_name);
    if(new_handler == NULL) {
        rm_log_warning_line(_("No such new_handler with this name: %s"), handler_name);
        return false;
    }

    g_return_val_if_fail(path, false);

    FILE *file_handle = NULL;
    bool needs_full_path = false;

    bool file_existed_already = false;
    if(g_strcmp0(path, "stdout") == 0) {
        file_handle = stdout;
        file_existed_already = true;
    } else if(g_strcmp0(path, "stderr") == 0) {
        file_handle = stderr;
        file_existed_already = true;
    } else if(g_strcmp0(path, "stdin") == 0) {
        /* I bet someone finds a use for this :-) */
        file_handle = stdin;
        file_existed_already = true;
    } else {
        needs_full_path = true;
        if(access(path, F_OK) == 0) {
            file_existed_already = true;
            if(self->session->cfg->backup) {
                rm_fmt_backup_old_result_file(self, path);
            }
        }

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

    new_handler_copy->file_existed_already = file_existed_already;

    if(needs_full_path == false) {
        new_handler_copy->path = g_strdup(path);
    } else {
        /* See this issue for more information:
         * https://github.com/sahib/rmlint/issues/212
         *
         * Anonymous pipes will fail realpath(), this means that actually
         * broken paths may fail later with this fix. The path for
         * pipes is for example "/proc/self/fd/12".
         * */
        char *full_path = realpath(path, NULL);
        if(full_path != NULL) {
            new_handler_copy->path = full_path;
        } else {
            new_handler_copy->path = g_strdup(path);
        }
    }

    g_hash_table_insert(self->handler_to_file, new_handler_copy, file_handle);
    g_hash_table_insert(self->path_to_handler, new_handler_copy->path, new_handler);
    g_hash_table_add(self->handler_set, (char *)new_handler_copy->name);
    g_queue_push_tail(self->handler_order, new_handler_copy);

    return true;
}

static void rm_fmt_write_impl(RmFile *result, RmFmtTable *self) {
    RM_FMT_FOR_EACH_HANDLER_BEGIN(self) {
        RM_FMT_CALLBACK(handler->elem, result);
    }
    RM_FMT_FOR_EACH_HANDLER_END
}

static gint rm_fmt_rank_size(const RmFmtGroup *ga, const RmFmtGroup *gb) {
    RmFile *fa = ga->files.head->data;
    RmFile *fb = gb->files.head->data;

    RmOff sa = fa->actual_file_size * (ga->files.length - 1);
    RmOff sb = fb->actual_file_size * (gb->files.length - 1);

    /* Better do not compare big unsigneds via a - b... */
    return SIGN_DIFF(sa, sb);
}

static int rm_lint_type_order[] = {
    /* Other types have a prio of 0 by default */
    [RM_LINT_TYPE_PART_OF_DIRECTORY]  = 1,
    [RM_LINT_TYPE_DUPE_DIR_CANDIDATE] = 2,
    [RM_LINT_TYPE_DUPE_CANDIDATE]     = 3,
};

static gint rm_fmt_rank(const RmFmtGroup *ga, const RmFmtGroup *gb, RmFmtTable *self) {
    const char *rank_order = self->session->cfg->rank_criteria;

    RmFile *fa = ga->files.head->data;
    RmFile *fb = gb->files.head->data;

    RM_DEFINE_PATH(fa);
    RM_DEFINE_PATH(fb);

    int fa_order = rm_lint_type_order[fa->lint_type];
    int fb_order = rm_lint_type_order[fb->lint_type];
    if(fa_order != fb_order) {
        return fa_order - fb_order;
    }

    /*  Sort by ranking */

    for(int i = 0; rank_order[i]; ++i) {
        gint r = 0;
        switch(tolower((unsigned char)rank_order[i])) {
        case 's':
            r = rm_fmt_rank_size(ga, gb);
            break;
        case 'a':
            r = strcasecmp(fa->folder->basename, fb->folder->basename);
            break;
        case 'm':
            r = FLOAT_SIGN_DIFF(fa->mtime, fb->mtime, MTIME_TOL);
            break;
        case 'p':
            r = SIGN_DIFF(fa->path_index, fb->path_index);
            break;
        case 'n':
            r = SIGN_DIFF(ga->files.length, gb->files.length);
            break;
        case 'o':
            r = SIGN_DIFF(ga->index, gb->index);
            break;
        }

        if(r != 0) {
            return isupper((unsigned char)rank_order[i]) ? -r : r;
        }
    }

    return 0;
}

void rm_fmt_flush(RmFmtTable *self) {
    RmCfg *cfg = self->session->cfg;
    if(!cfg->cache_file_structs) {
        return;
    }

    if(*(cfg->rank_criteria) || cfg->replay) {
        g_queue_sort(&self->groups, (GCompareDataFunc)rm_fmt_rank, self);
    }

    for(GList *iter = self->groups.head; iter; iter = iter->next) {
        RmFmtGroup *group = iter->data;
        g_queue_foreach(&group->files, (GFunc)rm_fmt_write_impl, self);
    }
}

void rm_fmt_close(RmFmtTable *self) {
    for(GList *iter = self->groups.head; iter; iter = iter->next) {
        RmFmtGroup *group = iter->data;
        rm_fmt_group_destroy(self, group);
    }

    g_queue_clear(&self->groups);

    RM_FMT_FOR_EACH_HANDLER_BEGIN(self) {
        RM_FMT_CALLBACK(handler->foot);
        fclose(file);
        g_mutex_clear(&handler->print_mtx);
    }
    RM_FMT_FOR_EACH_HANDLER_END

    g_hash_table_unref(self->name_to_handler);
    g_hash_table_unref(self->handler_to_file);
    g_hash_table_unref(self->path_to_handler);
    g_hash_table_unref(self->config);
    g_hash_table_unref(self->handler_set);
    g_queue_free(self->handler_order);
    g_rec_mutex_clear(&self->state_mtx);

    if(self->first_backup_timestamp) {
        g_date_time_unref(self->first_backup_timestamp);
    }

    g_slice_free(RmFmtTable, self);
}

void rm_fmt_write(RmFile *result, RmFmtTable *self) {
    bool direct = !(self->session->cfg->cache_file_structs);

    if(direct) {
        rm_fmt_write_impl(result, self);
    } else {
        if(result->is_original || self->groups.length == 0) {
            g_queue_push_tail(&self->groups, rm_fmt_group_new());
        }

        RmFmtGroup *group = self->groups.tail->data;
        group->index = self->groups.length - 1;

        g_queue_push_tail(&group->files, result);
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
        RM_FMT_FOR_EACH_HANDLER_BEGIN(self) {
            RM_FMT_CALLBACK(handler->prog, state);
        }
        RM_FMT_FOR_EACH_HANDLER_END
    }
    rm_fmt_unlock_state(self);
}

void rm_fmt_set_config_value(RmFmtTable *self, const char *formatter, const char *key,
                             const char *value) {
    GHashTable *key_to_vals = g_hash_table_lookup(self->config, formatter);

    if(key_to_vals == NULL) {
        key_to_vals = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_hash_table_insert(self->config, (char *)g_strdup(formatter), key_to_vals);
    }
    g_hash_table_insert(key_to_vals, (char *)key, (char *)value);
}

const char *rm_fmt_get_config_value(RmFmtTable *self, const char *formatter,
                                    const char *key) {
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

bool rm_fmt_has_formatter(RmFmtTable *self, const char *name) {
    GHashTableIter iter;
    char *handler_name = NULL;

    g_hash_table_iter_init(&iter, self->handler_set);

    while(g_hash_table_iter_next(&iter, (gpointer *)&handler_name, NULL)) {
        if(!g_strcmp0(handler_name, name)) {
            return true;
        }
    }

    return false;
}

bool rm_fmt_is_stream(_UNUSED RmFmtTable *self, RmFmtHandler *handler) {
    if(handler->path == NULL || strcmp(handler->path, "stdout") == 0 ||
       strcmp(handler->path, "stderr") == 0 || strcmp(handler->path, "stdin") == 0) {
        return true;
    }

    return access(handler->path, W_OK) == -1;
}
