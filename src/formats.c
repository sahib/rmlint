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
        [RM_PROGRESS_STATE_SUMMARY]    = "Finalizing",
        [RM_PROGRESS_STATE_N]          = "Unknown state"
    };

    return table[(state < RM_PROGRESS_STATE_N) ? state : RM_PROGRESS_STATE_N];
}

RmFmtTable *rm_fmt_open(RmSession *session) {
    RmFmtTable *self = g_slice_new0(RmFmtTable);
    self->name_to_handler = g_hash_table_new(g_str_hash, g_str_equal);
    self->handler_to_file = g_hash_table_new(NULL, NULL);
    self->session = session;

    extern RmFmtHandler *PROGRESS_HANDLER;
    rm_fmt_register(self, PROGRESS_HANDLER);

    extern RmFmtHandler *CSV_HANDLER;
    rm_fmt_register(self, CSV_HANDLER);

    extern RmFmtHandler *PRETTY_HANDLER;
    rm_fmt_register(self, PRETTY_HANDLER);

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
 
#define RM_FMT_CALLBACK(func, ...)                             \
    if(func) {                                                 \
        g_mutex_lock(&handler->print_mtx); {                   \
            func(self->session, handler, file, ##__VA_ARGS__); \
        }                                                      \
        g_mutex_unlock(&handler->print_mtx);                   \
    }                                                          \
 
bool rm_fmt_add(RmFmtTable *self, const char *handler_name, const char *path) {
    RmFmtHandler *new_handler = g_hash_table_lookup(self->name_to_handler, handler_name);
    if(new_handler == NULL) {
        g_printerr("No such new_handler with this name: %s\n", handler_name);
        return false;
    }

    size_t path_len = (path) ? strlen(path) : 0;
    FILE *file_handle = NULL;

    if(strncmp(path, "stdout", path_len) == 0) {
        file_handle = stdout;
    } else if(strncmp(path, "stdout", path_len) == 0) {
        file_handle = stderr;
    } else if(strncmp(path, "stdin", path_len) == 0) {
        /* I bet someone finds a use for this :-) */
        file_handle = stdin;
    } else {
        file_handle = fopen(path, "w");
    }

    if(file_handle == NULL) {
        g_printerr("Unable to open file for writing: %s\n", path);
        return false;
    }

    g_hash_table_insert(self->handler_to_file, new_handler, file_handle);

    if(new_handler->head) {
        new_handler->head(self->session, new_handler, file_handle);
    }

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
    g_slice_free(RmFmtTable, self);
}

void rm_fmt_write(RmFmtTable *self, RmFile *result) {
    RM_FMT_FOR_EACH_HANDLER(self) {
        RM_FMT_CALLBACK(handler->elem, result);
    }
}

void rm_fmt_set_state(RmFmtTable *self, RmFmtProgressState state, guint64 count, guint64 total) {
    RM_FMT_FOR_EACH_HANDLER(self) {
        RM_FMT_CALLBACK(handler->prog, state, count, total);
    }
}

#ifdef _RM_COMPILE_MAIN_OUTPUTS

int main(void) {
    RmSession session;
    RmFmtTable *table = rm_fmt_open(&session);
    if(!rm_fmt_add(table, "progressbar", "stdout")) {
        g_printerr("You've screwed up.\n");
        return EXIT_FAILURE;
    }

    g_usleep(1000 * 1000);

    for(int i = 0 ; i <= 50; ++i) {
        if(i <= 20) {
            rm_fmt_set_state(table, RM_PROGRESS_STATE_TRAVERSE, i, 0);
        } else if(i <= 25) {
            rm_fmt_set_state(table, RM_PROGRESS_STATE_PREPROCESS, 0, 0);
        } else if(i <= 95) {
            rm_fmt_set_state(table, RM_PROGRESS_STATE_SHREDDER, i, 95);
        } else {
            rm_fmt_set_state(table, RM_PROGRESS_STATE_SUMMARY, 0, 0);
        }

        RmFile dummy;
        rm_fmt_write(table, &dummy);

        g_usleep(1000 * 50);

    }

    g_usleep(1000 * 1000);

    rm_fmt_close(table);
    return EXIT_SUCCESS;
}

#endif
