#include <glib.h>
#include <string.h>
#include <fts.h>

#include "file.h"
#include "session.h"
#include "libart/art.h"

typedef struct RmTreeMerger {
    RmSession *session;
    art_tree tree;

    GQueue *failed_dirs;
    GQueue *merger_dirs;
} RmTreeMerger;

typedef struct RmDirectory {
    GQueue files;
    guint64 total_size;
    char *dir;
    bool is_happy;
} RmDirectory;

static RmDirectory *rm_directory_new(char *directory) {
    RmDirectory *self = g_malloc0(sizeof(RmDirectory));
    g_queue_init(&self->files);
    self->dir = directory;
    return self;
}

static void rm_directory_add(RmDirectory *self, RmFile *file) {
    g_queue_push_head(&self->files, file);
    self->total_size += file->file_size;
}

static void rm_directory_merge(RmDirectory *self, RmDirectory *other) {
    for(GList *iter = other->files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        g_queue_push_head(&self->files, file);
    }

    self->is_happy |= other->is_happy;
    self->total_size += other->total_size;
}

static void rm_directory_destroy(RmDirectory *self) {
    g_queue_clear(&self->files);
    g_free(self);
}

static RmTreeMerger *rm_tree_merger_new(RmSession *session) {
    RmTreeMerger * self = g_slice_new0(RmTreeMerger);
    self->session = session;
    init_art_tree(&self->tree);

    self->failed_dirs = g_queue_new();
    self->merger_dirs = g_queue_new();

    return self;
}

static void rm_tree_merger_destroy(RmTreeMerger *self) {
    destroy_art_tree(&self->tree);
    g_slice_free(RmTreeMerger, self);
}

static guint64 rm_tree_merger_du(RmTreeMerger *self, const char *dir) {
    guint64 result_size = 0;

    int fts_flags =  FTS_PHYSICAL | FTS_COMFOLLOW | FTS_NOCHDIR | FTS_NOSTAT;
    FTS *ftsp = fts_open((char *[2]) {(char *)dir, NULL }, fts_flags, NULL);

    if (ftsp == NULL) {
        rm_log_error("fts_open failed");
        return 0;
    }

    // TODO: Check for dirs/links/blockdevices
    FTSENT *p = NULL;
    while((p = fts_read(ftsp)) != NULL) {
        RmStat stat_buf;
        if(rm_sys_stat(p->fts_path, &stat_buf) != -1) {
            if(S_ISREG(stat_buf.st_mode)) {
                result_size += stat_buf.st_size;
            }
        }
    }

    fts_close(ftsp);
    return result_size;
}
static void rm_tree_merger_add(RmTreeMerger *self, RmFile *file) {
    char *dirname = g_path_get_dirname(file->path);
    gsize dirname_len = strlen(file->path);

    RmDirectory *child = art_search(&self->tree, dirname, dirname_len);
    if(child == NULL) {
        child = rm_directory_new(dirname);
        art_insert(&self->tree, dirname, dirname_len, child);
    }

    rm_directory_add(child, file);
} 

int merge_callback(RmTreeMerger *self, const char * dir_path, guint32 dir_len, RmDirectory *dir) {
    guint64 real_size = rm_tree_merger_du(self, dir_path);
    if(real_size != dir->total_size) {
        g_queue_push_head(self->failed_dirs, dir);
    } else {
        dir->is_happy = true;  /* at least one sucessfull merges */
        g_queue_push_head(self->merger_dirs, dir);
    }

    return 0;
}

static void rm_tree_merger_merge(RmTreeMerger *self) {
    art_iter(&self->tree, (art_callback)merge_callback, self);

    for(GList *iter = self->failed_dirs->head; iter; iter = iter->next) {
        RmDirectory *child= iter->data;
        if(child->is_happy) {
            g_printerr("Dir: %s\n", child->dir);
        } else {
            for(GList *file_iter = child->files.head; file_iter; file_iter = file_iter->next) {
                RmFile *file = file_iter->data;
                g_printerr("File: %s\n", file->path);
            }
        }

        art_delete(&self->tree, child->dir, strlen(child->dir));
    }

    for(GList *iter = self->merger_dirs->head; iter; iter = iter->next) {
        RmDirectory *child = iter->data;
        art_delete(&self->tree, child->dir, strlen(child->dir));

        child->dir = g_path_get_dirname(child->dir);
        if(child->dir == NULL) {
            continue;
        }

        RmDirectory *parent = art_insert(&self->tree, child->dir, strlen(child->dir), child);
        if(parent != NULL) {
            rm_directory_merge(child, parent);
            rm_directory_destroy(parent);
        }
    }
}
