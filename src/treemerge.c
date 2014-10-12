#include <glib.h>
#include <string.h>
#include <fts.h>

#include "file.h"
#include "session.h"
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
    guint64 common_hash;       /* TODO */
    guint32 file_count;        /* Count of files actually in this directory            */
    guint8  finished;          /* Was this dir or one of his parents already printed?  */
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
    memset(path, 0, sizeof(path));
    memcpy(path, key, key_len);

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

static bool rm_tm_count_files(art_tree *dir_tree, char **files, int bit_flags) {
    if (*files == NULL) {
        rm_log_error("No files passed to rm_tm_count_files\n");
        return false;
    }

    FTS *fts = fts_open(files, bit_flags, NULL);
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
        // TODO: Use same settings as traverse.c
        if(ent->fts_info == FTS_F) {
            art_insert(
                &file_tree, (unsigned char *)ent->fts_path, ent->fts_pathlen + 1, NULL
            );
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

#ifdef _RM_COMPILE_MAIN_TM

static int print_iter(_U void * data, const unsigned char * key, _U uint32_t key_len, _U void * value) {
    int level = -1;
    for(int i = 0; i < key_len; ++i) {
        if(key[i] == '/') level ++;
    }
    g_printerr("%04u", GPOINTER_TO_UINT(value));
    for(int i = 0; i < level; ++i) {
        g_printerr("  ");
    }

    g_printerr("%s\n", key);
    return 0;
}

int main(int argc, const char **argv) {
    if(argc < 2) {
        g_printerr("Go pass me some data\n");
    }

    char *dirs[2] = {(char *) argv[1], NULL};
    art_tree dir_tree;
    init_art_tree(&dir_tree);

    rm_tm_count_files(&dir_tree, dirs, 0);
    art_iter(&dir_tree, print_iter, NULL);
    destroy_art_tree(&dir_tree);
}

#endif

///////////////////////////////
// DIRECTORY STRUCT HANDLING //
///////////////////////////////

static RmDirectory * rm_directory_new(char *dirname) {
    RmDirectory * self = g_new0(RmDirectory, 1);   
    
    self->common_hash = 0;

    g_queue_init(&self->known_files);
    g_queue_init(&self->children);

    self->dirname = dirname;
    self->finished = false;
    self->level = 0;

    init_art_tree(&self->hash_trie);

    return self;
}

static void rm_directory_free(RmDirectory *self) {
    destroy_art_tree(&self->hash_trie);
    g_queue_clear(&self->known_files);
    g_queue_clear(&self->children);
    g_free(self);
}

static int rm_directory_equal_iter(RmDirectory *other, const unsigned char * key, uint32_t key_len, void * value) {
    return !GPOINTER_TO_UINT(art_search(&other->hash_trie, (unsigned char *)key, key_len));
}

static bool rm_directory_equal(RmDirectory *d1, RmDirectory *d2) {
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
    return d->common_hash;
}

static void rm_directory_add(RmDirectory *directory, RmFile *file) {
    /* Add the file to this directory */   
    g_queue_push_head(&directory->known_files, file);
    
    /* Update the directorie's hash with the file's hash
       Since we cannot be sure in which order the files come in
       we have to add the hash cummulatively.
     */
    guint8 *file_digest = rm_digest_steal_buffer(file->digest);
    directory->common_hash ^= *((guint64 *)file_digest);
    g_slice_free1(file->digest->bytes, file_digest);
}

///////////////////////////
// TREE MERGER ALGORITHM //
///////////////////////////

RmTreeMerger * rm_tm_new(RmSession *session) {
    // TODO: 
    // - Make hash and equal function sanely defined
}

static void rm_tm_insert_dir(RmTreeMerger *self, RmDirectory *directory) {
    GQueue *dir_queue = g_hash_table_lookup(self->result_table, directory);
    if(dir_queue == NULL) {
        dir_queue = g_queue_new();
        g_hash_table_insert(self->result_table, directory, dir_queue);
    }

    g_queue_push_head(dir_queue, directory);
}

static void rm_tm_feed(RmTreeMerger *self, RmFile *file) {
    char *dirname = g_path_get_dirname(file->path);
    guint dir_len = strlen(dirname) + 1;
    bool free_dir = true;
    
    /* See if we know that directory already */    
    RmDirectory *directory = art_search(&self->dir_tree, dirname, dir_len);
    if(directory == NULL) {
        directory = rm_directory_new(dirname);

        /* Get the actual file count */
        directory->file_count = GPOINTER_TO_UINT(
            art_search(&self->count_tree, dirname, dir_len
        ));

        /* Make the new directory known */
        art_insert(&self->dir_tree, dirname, dir_len, directory);

        g_queue_push_head(&self->valid_dirs, directory);
    } 

    rm_directory_add(directory, file);
 
    /* Check if the directory reached the number of actual files in it */
    if(directory->known_files.length == directory->file_count) {
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

static int rm_tm_sort_paths(const RmDirectory *da, const RmDirectory *db, _U void *user_data) {
    int depth_balance = 0;
    char *a = da->dirname, *b = db->dirname;

    for(int i = 0; a[i] && b[i]; ++i) {
        depth_balance += (a[i] == '/');
        depth_balance -= (b[i] == '/');
    }    

    return CLAMP(depth_balance, -1, +1);
}

static void rm_tm_extract(RmTreeMerger *self) {
    /* Go back from highest level to lowest */
    GHashTable *result_table = self->result_table;
    
    GQueue *dir_list = NULL;

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, result_table);

    /* Iterate over all directories per hash (which are same therefore) */
    while(g_hash_table_iter_next(&iter, NULL, (void **)&dir_list)) {
        /* Sort the RmDirectory list by their path depth, lowest depth first */
        g_queue_sort(dir_list, (GCompareDataFunc)rm_tm_sort_paths, NULL);

        /* Output the directories and mark their children to prevent 
         * duplicate directory reports. 
         */
        for(GList *iter = dir_list->head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            if(directory->finished == false) {
                rm_tm_mark_finished(directory);
                g_printerr("%s\n", directory->dirname);
            }
        }
        g_printerr("--\n");
    }
}

static void rm_tm_finish(RmTreeMerger *self) {
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
            RmDirectory *parent = art_search(&self->dir_tree, parent_dir, parent_len);
        
            if(parent == NULL) {
                /* none yet, basically copy child */
                parent = rm_directory_new(parent_dir);
                art_insert(&self->dir_tree, parent_dir, parent_len, parent);

                /* Get the actual file count */
                directory->file_count = GPOINTER_TO_UINT(
                    art_search(&self->count_tree, parent_dir, parent_len 
                ));

                g_queue_push_head(&new_dirs, directory);               
            } 

            /* Copy children to parent */
            for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
                rm_directory_add(parent, (RmFile *)iter->data);
            }

            g_queue_push_head(&parent->children, directory);
        } 
        
        /* Keep those level'd up dirs that are full now. 
           Dirs that are not full until now, won't either in higher levels.
         */
        g_queue_clear(&self->valid_dirs);
        for(GList *iter = new_dirs.head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            if(directory->known_files.length == directory->file_count) {
                g_queue_push_head(&self->valid_dirs, directory);
                rm_tm_insert_dir(self, directory);
            }
        }
        g_queue_clear(&new_dirs);
    }

    /* Fish the result dirs out of the result table */
    rm_tm_extract(self);
}
