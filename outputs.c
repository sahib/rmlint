#include <stdlib.h>
#include <string.h>

#include "outputs.h"

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
    return self;
}

void rm_fmt_register(RmFmtTable *self, RmFmtHandler *handler, const char *name) {
    g_hash_table_insert(self->name_to_handler, (char *) name, handler);
}

#define RM_FMT_FOR_EACH_HANDLER(self)                                             \
    FILE * file = NULL;                                                           \
    RmFmtHandler * handler = NULL;                                                \
                                                                                  \
    GHashTableIter iter;                                                          \
    g_hash_table_iter_init(&iter, self->handler_to_file);                         \
    while(g_hash_table_iter_next(&iter, (gpointer *)&handler, (gpointer *)&file)) \
 
#define RM_FMT_CALLBACK(func, ...)                         \
    if(func) {                                             \
        func(self->session, handler, file, ##__VA_ARGS__); \
    }                                                      \
 
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

typedef struct RmFmtHandlerProgress {
    /* must be first */
    RmFmtHandler parent;

    /* user data */
    char percent;
    RmFmtProgressState last_state;
    guint64 n, N;
} RmFmtHandlerProgress;

static void rm_fmt_head(G_GNUC_UNUSED RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out) {
    fprintf(out, " Hi, Im a progressbar!\r");
    fflush(out);
}

static void rm_fmt_elem(G_GNUC_UNUSED RmSession *session, RmFmtHandler *parent, FILE *out, G_GNUC_UNUSED RmFile *file) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    if(self->percent > 100) {
        self->percent = 100;
    }

    fprintf(out, " [");

    for(int i = 0; i < self->percent; ++i) {
        if(i == self->percent - 1) {
            fprintf(out, "->");
        } else {
            fprintf(out, "-");
        }
    }

    int left = 100 - self->percent;
    for(int i = 0; i < left; ++i)  {
        fprintf(out, " ");
    }

    fprintf(out, "] %-30s (%lu/%lu)    \r", rm_fmt_progress_to_string(self->last_state), self->n , self->N);
    fflush(out);

    self->percent++;
}

static void rm_fmt_prog(
    G_GNUC_UNUSED RmSession *session,
    RmFmtHandler *parent,
    G_GNUC_UNUSED FILE *out,
    RmFmtProgressState state,
    guint64 n, guint64 N
) {
    RmFmtHandlerProgress *self = (RmFmtHandlerProgress *) parent;
    self->n = n;
    self->N = N;
    self->last_state = state;
}

static void rm_fmt_foot(G_GNUC_UNUSED RmSession *session, G_GNUC_UNUSED RmFmtHandler *parent, FILE *out) {
    fprintf(out, "End of demonstration.%150s\n", " ");
    fflush(out);
}

static RmFmtHandlerProgress PROGRESS_HANDLER = {
    /* Initialize parent */
    .parent = {
        .name = "progressbar",
        .head = rm_fmt_head,
        .elem = rm_fmt_elem,
        .prog = rm_fmt_prog,
        .foot = rm_fmt_foot
    },

    /* Initialize own stuff */
    .percent = 0,
    .last_state = RM_PROGRESS_STATE_INIT
};

int main(void) {
    RmSession session;
    RmFmtTable *table = rm_fmt_open(&session);
    rm_fmt_register(table, (RmFmtHandler *) &PROGRESS_HANDLER, "progressbar");
    if(!rm_fmt_add(table, "progressbar", "stdout")) {
        g_printerr("You've screwed up.\n");
        return EXIT_FAILURE;
    }

    g_usleep(1000 * 1000);

    for(int i = 0 ; i <= 100; ++i) {
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
