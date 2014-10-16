#include <glib.h>
#include <string.h>
#include <fts.h>

#include "file.h"
#include "session.h"
#include "preprocess.h"
#include "libart/art.h"

typedef struct RmTreeMerger {
    RmSession *session;        /* Session state variables / Settings       */
    art_tree dir_tree;         /* Path-Trie with all RmFiles as value      */
    art_tree count_tree;       /* Path-Trie with all file's count as value */
    GHashTable *result_table;  /* {hash => [RmDirectory]} mapping          */
    GQueue valid_dirs;         /* Directories consisting of RmFiles only   */
} RmTreeMerger;

typedef struct RmDirectory {
    char *dirname;             /* Path to this directory without trailing slash        */
    GQueue known_files;        /* RmFiles in this directory                            */
    GQueue children;           /* Children for directories with subdirectories         */
    guint32 prefd_files;       /* Files in this directory that are tagged as original  */
    guint64 common_hash;       /* Short hash for this directory, used as HT-Index      */
    guint32 dupe_count;        /* Count of RmFiles actually in this directory          */
    guint32 file_count;        /* Count of files actually in this directory            */
    bool finished;             /* Was this dir or one of his parents already printed?  */
    art_tree hash_trie;        /* Trie of hashes, used for equality check (to be sure) */
} RmDirectory;

//////////////////////////
// ACTUAL FILE COUNTING //
//////////////////////////

static int rm_tm_count_art_callback(void * data, const unsigned char * key, uint32_t key_len, _U void * value) {
    /* Note: this method has a time complexity of O(log(n) * m) which may
       result in a few seconds buildup time for large sets of directories.  Since this
       will only happen when rmlint ran for long anyways and since we can keep the
       code easy and memory efficient this way, Im against more clever but longer
       solutions. (Good way of saying "Im just too stupid", eh?)
     */
    art_tree *dir_tree = data;

    unsigned char path[PATH_MAX];
    memcpy(path, key, key_len);
    path[key_len] = 0;

    /* Ascend the path parts up, add one for each part we meet.
       If a part was never found before, add it.
       This is the 'm' above: The count of separators in the path.

       Hack: path[key_len] is nul, at key_len it must be either an
             extra slash (bad) or the beginning of a file name.
             Therefore start at -2.
     */
    for(int i = key_len - 2; i >= 0; --i) {
        if(path[i] == G_DIR_SEPARATOR) {
            /* Do not use an empty path, use a slash for root */
            if(i == 0) {
                path[0] = '/'; path[1] = 0;
            } else {
                path[i] = 0;
            }

            /* Include the nulbyte */
            int new_key_len = MAX(0, i - 1) + 2;

            /* Accumulate the count ('n' above is the height of the trie)  */
            art_insert(
                dir_tree, (unsigned char *)path, new_key_len,
                GUINT_TO_POINTER(
                    GPOINTER_TO_UINT(art_search(dir_tree, path, new_key_len)) + 1
                )
            );
        }
    }

    return 0;
}

static bool rm_tm_count_files(art_tree *dir_tree, char **files, RmSession *session) {
    if (*files == NULL) {
        rm_log_error("No files passed to rm_tm_count_files\n");
        return false;
    }

    int fts_flags = FTS_COMFOLLOW;
    if(session->settings->followlinks) {
        fts_flags |= FTS_LOGICAL;
    } else {
        fts_flags |= FTS_PHYSICAL;
    }

    FTS *fts = fts_open(files, fts_flags, NULL);
    if(fts == NULL) {
        rm_log_perror("fts_open failed");
        return false;
    }

    /* This tree stores the full file paths.
       It is joined into a full directory tree later.
     */
    art_tree file_tree;
    init_art_tree(&file_tree);

    FTSENT *ent = NULL;
    while((ent = fts_read(fts))) {
        /* Handle large files (where fts fails with FTS_NS) */
        if(ent->fts_info == FTS_NS) {
            RmStat stat_buf;
            if(rm_sys_stat(ent->fts_path, &stat_buf) == -1) {
                rm_log_perror("stat(2) failed");
                continue;
            } else {
                /* Must be a large file (or followed link to it) */
                ent->fts_info = FTS_F;
            }
        }

        switch(ent->fts_info) {
            case FTS_F:
            case FTS_SL:
            case FTS_SLNONE:
                art_insert(
                    &file_tree, (unsigned char *)ent->fts_path, ent->fts_pathlen + 1, NULL
                );
            default:
                break;
        }
    }

    if (fts_close (fts) != 0) {
        rm_log_perror("fts_close failed");
        return false;
    }

    art_iter(&file_tree, rm_tm_count_art_callback, dir_tree);
    destroy_art_tree(&file_tree);
    return true;
}

///////////////////////////////
// DIRECTORY STRUCT HANDLING //
///////////////////////////////

static RmDirectory * rm_directory_new(char *dirname) {
    RmDirectory * self = g_new0(RmDirectory, 1);   
    
    self->file_count = 0;
    self->dupe_count = 0;
    self->prefd_files = 0;
    self->common_hash = 0;

    g_queue_init(&self->known_files);
    g_queue_init(&self->children);

    self->dirname = dirname;
    self->finished = false;

    init_art_tree(&self->hash_trie);

    return self;
}

static void rm_directory_free(RmDirectory *self) {
    destroy_art_tree(&self->hash_trie);
    g_queue_clear(&self->known_files);
    g_queue_clear(&self->children);
    g_free(self->dirname);
    g_free(self);
}

static int rm_directory_equal_iter(
    art_tree *other_hash_trie, const unsigned char * key, uint32_t key_len, _U void * value
) {
    return !GPOINTER_TO_UINT(art_search(other_hash_trie, (unsigned char *)key, key_len));
}

static bool rm_directory_equal(RmDirectory *d1, RmDirectory *d2) {
    /* TODO: Will this work with paranoid settings? Probably, but in a weird way. */
    if(d1->common_hash != d2->common_hash) {
        return false;
    }

    if(art_size(&d1->hash_trie) != art_size(&d2->hash_trie)) {
        return false;
    }

    /* Take the bitter pill and compare all hashes manually.
     * This should only happen on hash collisions of common_hash.
     */
    return !art_iter(&d1->hash_trie, (art_callback)rm_directory_equal_iter, &d2->hash_trie);
}

static guint rm_directory_hash(const RmDirectory *d) {
    /* This hash is used to quickly compare directories with each other.
     * Different directories might yield the same hash of course.
     * To prevent this case, rm_directory_equal really compares
     * all the file's hashes with each other. 
     */
    return d->common_hash ^ d->dupe_count;
}

static void rm_directory_add(RmDirectory *directory, RmFile *file) {
    /* Update the directorie's hash with the file's hash
       Since we cannot be sure in which order the files come in
       we have to add the hash cummulatively.
     */
    guint8 *file_digest = rm_digest_steal_buffer(file->digest);

    /* + and not XOR, since ^ would yield 0 for same hashes always. No matter
     * which hashes. Also this would be confusing. For me and for debuggers.
     */
    directory->common_hash += *((guint64 *)file_digest);

    /* The file value is not really used, but we need some non-null value */
    art_insert(&directory->hash_trie, file_digest, file->digest->bytes, file); 
    g_slice_free1(file->digest->bytes, file_digest);

    directory->dupe_count += 1;
    directory->prefd_files += file->is_prefd;
}

static void rm_directory_add_subdir(RmDirectory *parent, RmDirectory *subdir) {
    parent->file_count += subdir->file_count;
    parent->prefd_files += subdir->prefd_files;
    g_queue_push_head(&parent->children, subdir);

    for(GList *iter = subdir->known_files.head; iter; iter = iter->next) {
        rm_directory_add(parent, (RmFile *)iter->data);
    }
}

///////////////////////////
// TREE MERGER ALGORITHM //
///////////////////////////

RmTreeMerger * rm_tm_new(RmSession *session) {
    RmTreeMerger * self = g_slice_new(RmTreeMerger);
    self->session = session;
    g_queue_init(&self->valid_dirs);

    self->result_table = g_hash_table_new_full(
        (GHashFunc)rm_directory_hash, 
        (GEqualFunc)rm_directory_equal, 
        NULL,
        (GDestroyNotify)g_queue_free
    );

    init_art_tree(&self->dir_tree);
    init_art_tree(&self->count_tree);

    rm_tm_count_files(&self->count_tree, session->settings->paths, session);

    return self;
}

static int rm_tm_destroy_iter(
    _U void * data, _U const unsigned char * key, _U uint32_t key_len,  void * value
) {
    rm_directory_free((RmDirectory *)value);
    return 0;
}

void rm_tm_destroy(RmTreeMerger *self) {
    g_hash_table_unref(self->result_table);
    g_queue_clear(&self->valid_dirs);

    art_iter(&self->dir_tree, rm_tm_destroy_iter, NULL);
    destroy_art_tree(&self->dir_tree);
    destroy_art_tree(&self->count_tree);
    g_slice_free(RmTreeMerger, self);
}

static void rm_tm_insert_dir(RmTreeMerger *self, RmDirectory *directory) {
    GQueue *dir_queue = g_hash_table_lookup(self->result_table, directory);
    if(dir_queue == NULL) {
        dir_queue = g_queue_new();
        g_hash_table_insert(self->result_table, directory, dir_queue);
    }

    g_queue_push_head(dir_queue, directory);
}

void rm_tm_feed(RmTreeMerger *self, RmFile *file) {
    char *dirname = g_path_get_dirname(file->path);
    guint dir_len = strlen(dirname) + 1;
    
    /* See if we know that directory already */    
    RmDirectory *directory = art_search(
        &self->dir_tree, (unsigned char *)dirname, dir_len
    );

    if(directory == NULL) {
        directory = rm_directory_new(dirname);

        /* Get the actual file count */
        directory->file_count = GPOINTER_TO_UINT(
            art_search(&self->count_tree, (unsigned char *)dirname, dir_len
        ));

        /* Make the new directory known */
        art_insert(&self->dir_tree, (unsigned char *)dirname, dir_len, directory);

        g_queue_push_head(&self->valid_dirs, directory);
    } else {
        g_free(dirname);
    }

    rm_directory_add(directory, file);

    /* Add the file to this directory */   
    g_queue_push_head(&directory->known_files, file);
 
    /* Check if the directory reached the number of actual files in it */
    if(directory->dupe_count == directory->file_count) {
        g_printerr("Encountered full directory: %s\n", directory->dirname);
        rm_tm_insert_dir(self, directory);
    }
}

static void rm_tm_mark_finished(RmDirectory *directory) {
    directory->finished = true;

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        rm_tm_mark_finished((RmDirectory *)iter->data);
    }
}

static int rm_tm_sort_paths(const RmDirectory *da, const RmDirectory *db, _U RmTreeMerger *self) {
    int depth_balance = 0;
    char *a = da->dirname, *b = db->dirname;

    for(int i = 0; a[i] && b[i]; ++i) {
        depth_balance += (a[i] == '/');
        depth_balance -= (b[i] == '/');
    }    

    return depth_balance;
}

static int rm_tm_sort_orig_criteria(const RmDirectory *da, const RmDirectory *db, RmTreeMerger *self) {
    RmStat dir_stat_a, dir_stat_b; 
    if(rm_sys_stat(da->dirname, &dir_stat_a) == -1) {
        rm_log_perror("stat failed during sort");
        return 0;
    }
    if(rm_sys_stat(db->dirname, &dir_stat_b) == -1) {
        rm_log_perror("stat failed during sort");
        return 0;
    }
    
    return rm_pp_cmp_orig_criteria_impl(
        self->session,
        dir_stat_a.st_mtime, dir_stat_b.st_mtime,
        rm_util_basename(da->dirname), rm_util_basename(db->dirname),
        db->prefd_files, da->prefd_files
 
    );
}

static void rm_tm_forward_unresolved(RmDirectory *directory) {
    if(directory->finished == true) {
        return; 
    } else {
        directory->finished = true;
    }

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile * file = iter->data;
        g_printerr("  %s\n", file->path);
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        rm_tm_forward_unresolved((RmDirectory *)iter->data);
    }

    g_printerr("--\n");
}

static int rm_tm_iter_unfinished_files(
    _U RmTreeMerger * self, _U const unsigned char * key, _U uint32_t key_len, RmDirectory *directory
) {
    rm_tm_forward_unresolved(directory);
    return 0;
}

static void rm_tm_extract(RmTreeMerger *self) {
    /* Go back from highest level to lowest */
    GHashTable *result_table = self->result_table;
    
    GQueue *dir_list = NULL;

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, result_table);

    g_printerr("\nResults:\n\n");

    /* Iterate over all directories per hash (which are same therefore) */
    while(g_hash_table_iter_next(&iter, NULL, (void **)&dir_list)) {
        if(dir_list->length < 2) {
            continue;
        }

        /* List of result directories */
        GQueue result_dirs = G_QUEUE_INIT;

        /* Sort the RmDirectory list by their path depth, lowest depth first */
        g_queue_sort(dir_list, (GCompareDataFunc)rm_tm_sort_paths, self);

        /* Output the directories and mark their children to prevent 
         * duplicate directory reports. 
         */
        for(GList *iter = dir_list->head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            if(directory->finished == false) {
                rm_tm_mark_finished(directory);
                g_queue_push_head(&result_dirs, directory);
            }
        }

        g_queue_sort(&result_dirs, (GCompareDataFunc)rm_tm_sort_orig_criteria, self);

        for(GList *iter = result_dirs.head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            g_printerr("%lx %s\n", directory->common_hash, directory->dirname);
        }
        g_printerr("--\n");
        g_queue_clear(&result_dirs);
    }

    g_printerr("\nDupes among other files:\n\n");

    /* Iterate over all non-finished dirs in the tree,
     * and print their unfinished non-matching files 
     */
    art_iter(&self->dir_tree, (art_callback)rm_tm_iter_unfinished_files, self);
}

void rm_tm_finish(RmTreeMerger *self) {
    while(self->valid_dirs.length > 0) {
        GQueue new_dirs = G_QUEUE_INIT;

        /* Iterate over all valid directories and try to level them one 
           layer up. If there's already one one layer up, we'll merge with it.
         */
        for(GList *iter = self->valid_dirs.head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            char *parent_dir = g_path_get_dirname(directory->dirname);
            gsize parent_len = strlen(parent_dir) + 1;

            /* Lookup if we already found this parent before (if yes, merge with it) */
            RmDirectory *parent = art_search(
                &self->dir_tree, (unsigned char *)parent_dir, parent_len
            );
        
            if(parent == NULL) {
                /* none yet, basically copy child */
                parent = rm_directory_new(parent_dir);
                art_insert(
                    &self->dir_tree, (unsigned char *)parent_dir, parent_len, parent
                );

                /* Get the actual file count */
                directory->file_count = GPOINTER_TO_UINT(
                    art_search(&self->count_tree, (unsigned char *)parent_dir, parent_len 
                ));

                g_queue_push_head(&new_dirs, directory);               
            } else {
                g_free(parent_dir);
            }

            rm_directory_add_subdir(parent, directory);
        } 
        
        /* Keep those level'd up dirs that are full now. 
           Dirs that are not full until now, won't be in higher levels.
         */
        g_queue_clear(&self->valid_dirs);
        for(GList *iter = new_dirs.head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            if(directory->dupe_count == directory->file_count) {
                g_queue_push_head(&self->valid_dirs, directory);
                rm_tm_insert_dir(self, directory);
            }
        }
        g_queue_clear(&new_dirs);
    }

    /* Fish the result dirs out of the result table */
    rm_tm_extract(self);
}

#ifdef _RM_COMPILE_MAIN_TM_ALL

static int print_iter(_U void * data, const unsigned char * key, _U uint32_t key_len, _U void * value) {
    int level = -1;
    for(unsigned i = 0; i < key_len; ++i) {
        if(key[i] == '/') level ++;
    }
    g_printerr("%4u", GPOINTER_TO_UINT(value));
    for(int i = 0; i < level + 1; ++i) {
        g_printerr("  ");
    }

    g_printerr("%s\n", key);
    return 0;
}

int main(_U int argc, char **argv) {
    RmSession session;
    RmSettings settings;

    char *argv_copy[argc + 1];
    memset(argv_copy, 0, sizeof(argv_copy));

    for(int i = 0; i < argc - 1; ++i) {
        argv_copy[i] = argv[i + 1];
    }

    for(int i = 0; argv_copy[i]; ++i) {
        g_printerr("%s:\n", argv_copy[i]);
    }

    session.settings = &settings;
    settings.paths = argv_copy;
    settings.sort_criteria = "A";
    RmTreeMerger *merger = rm_tm_new(&session);

    art_iter(&merger->count_tree, print_iter, NULL);

    g_printerr("\n");

    GQueue file_queue = G_QUEUE_INIT;

    char path[PATH_MAX] = {0};
    while(fgets(path, PATH_MAX, stdin)) {
        RmFile *dummy = g_new0(RmFile, 1);
        dummy->digest = rm_digest_new(RM_DIGEST_MURMUR, 0, 0, 0);
        path[strlen(path) - 1] = 0;

        FILE *handle = fopen(path, "r");
        if(handle) {
            unsigned char buffer[4096]; 
            memset(buffer, 0, sizeof(buffer));
            int bytes_read = 0;
            while((bytes_read = fread(buffer, 1, sizeof(buffer), handle)) > 0) {
                rm_digest_update(dummy->digest, buffer, bytes_read);
            }

            char hex[100];
            rm_digest_hexstring(dummy->digest, hex);
            g_printerr("Adding %20s %s\n", path, hex);

            g_queue_push_head(&file_queue, dummy);
            dummy->path = g_strdup(path);
            rm_tm_feed(merger, dummy);
        } else {
            g_printerr("Unable to read: %s\n", path);
            continue;
        }

    }

    rm_tm_finish(merger);
    rm_tm_destroy(merger);

    for(GList *iter = file_queue.head; iter; iter = iter->next) {
        RmFile *dummy = iter->data;
        rm_digest_free(dummy->digest);
        g_free(dummy->path);
        g_free(dummy);
    }

    g_queue_clear(&file_queue);

    return 0;
}

#endif
