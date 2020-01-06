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

/* This is the treemerge algorithm.
 *
 * It tries to solve the following problem and sometimes even succeeds:
 * Take a list of duplicates (as RmFiles) and figure out which directories
 * consist fully out of equal duplicates and can be thus removed.
 * It does NOT care about paths or filesystem layout by default.
 *
 * The basic algorithm is split in four phases:
 *
 * - Counting:  Walk through all directories given on the commandline and
 *              traverse them. Count all files during traverse and safe it in
 *              an radix-tree (libart is used here). The key is the path, the
 *              value the count of files in it. Invalid directories and
 *              directories above the given are set to -1.
 *              This step happens before shreddering files.
 *
 *              Result of this step: A trie that contains all directories with
 *              the count of files in it.
 *
 * - Feeding:   Collect all duplicates and store them in RmDirectory structures.
 *              If a directory appears to consist of dupes only (num_dupes == num_files)
 *              then it is remembered as valid directory. This step happens in parallel
 *              to shreddering files.
 *
 *              Result of this step: A list of directories that contain only duplicates
 *              and duplicates that are separated from that.
 *
 * - Upcluster: Take all valid directories and cluster them up, so subdirs get
 *              merged into the parent directory. Continue as long the parent
 *              directory is full too. Remember full directories in a hashtable
 *              with the hash of the directory (which is a hash of the file's
 *              hashes) as key and a list of matching directories as value.
 *
 *              Result of this step: A hashtable with equal directories
 *              (that however may contain other equal directories)
 *
 * - Extract:   Extract the result information out of the hashtable top-down.
 *              If a directory is reported, mark all subdirs of it as finished
 *              so they do not get reported twice. Files that could not be
 *              grouped in directories are found and reported as usually.
 *              Some ugly and tricky parts are in here due to the many options of rmlint.
 */

/*
 * Comment this out to see helpful extra debugging:
 */
// #define _RM_TREEMERGE_DEBUG

#include <glib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "formats.h"
#include "pathtricia.h"
#include "preprocess.h"
#include "shredder.h"
#include "treemerge.h"
#include "session.h"

#include "fts/fts.h"

typedef struct RmDirectory {
    char *dirname;       /* Path to this directory without trailing slash              */
    GQueue known_files;  /* RmFiles in this directory                                  */
    GQueue children;     /* Children for directories with subdirectories               */
    gint64 prefd_files;  /* Files in this directory that are tagged as original        */
    gint64 dupe_count;   /* Count of RmFiles actually in this directory                */
    gint64 file_count;   /* Count of files actually in this directory (or -1 on error) */
    gint64 mergeups;     /* number of times this directory was merged up
                            This is used to find the highest ranking directory. */
    bool finished : 1;   /* Was this dir or one of his parents already printed?        */
    bool was_merged : 1; /* true if this directory was merged up already (only once)   */
    bool was_inserted : 1; /* true if this directory was added to results (only once)  */
    bool was_dupe_extracted : 1;
    unsigned short depth; /* path depth (i.e. count of / in path, no trailing /)       */
    GHashTable *hash_set; /* Set of hashes, used for true equality check               */
    RmDigest *digest;     /* Common XOR digest of all RmFiles in this directory.
                             note that this is only used as fast hash comparison.      */

    struct {
        gdouble dir_mtime; /* Directory Metadata: Modification Time */
        ino_t dir_inode;   /* Directory Metadata: Inode             */
        dev_t dir_dev;     /* Directory Metadata: Device ID         */
    } metadata;
} RmDirectory;

const char *rm_directory_get_dirname(RmDirectory *self) {
    return self->dirname;
}

struct RmTreeMerger {
    RmSession *session;              /* Session state variables / Settings                  */
    RmTrie dir_tree;                 /* Path-Trie with all RmFiles as value                 */
    RmTrie count_tree;               /* Path-Trie with all file's count as value            */
    GHashTable *result_table;        /* {hash => [RmDirectory]} mapping                     */
    GHashTable *file_groups;         /* Group files by hash                                 */
    GHashTable *file_checks;         /* Set of files that were handled already.             */
    GHashTable *known_hashs;         /* Set of known hashes, only used for cleanup.         */
    GHashTable *free_map;            /* Map of file pointer to RmFile (used to cleanup) */
    GQueue valid_dirs;               /* Directories consisting of RmFiles only              */
    RmTreeMergeOutputFunc callback;  /* Callback for finished directories or leftover files */
    gpointer callback_data;
};

//////////////////////////
// ACTUAL FILE COUNTING //
//////////////////////////

int rm_tm_count_art_callback(_UNUSED RmTrie *self, RmNode *node, _UNUSED int level,
                             void *user_data) {
    /* Note: this method has a time complexity of O(log(n) * m) which may
       result in a few seconds buildup time for large sets of directories.  Since this
       will only happen when rmlint ran for long anyways and since we can keep the
       code easy and memory efficient this way, Im against more clever but longer
       solutions. (Good way of saying "Im just too stupid", eh?)
    */

    RmTrie *count_tree = user_data;
    bool error_flag = GPOINTER_TO_INT(node->data);

    char path[PATH_MAX];
    memset(path, 0, sizeof(path));
    rm_trie_build_path_unlocked(node, path, sizeof(path));

    /* Ascend the path parts up, add one for each part we meet.
       If a part was never found before, add it.
       This is the 'm' above: The count of separators in the path.

       Hack: path[key_len] is nul, at key_len it must be either an
             extra slash (bad) or the beginning of a file name.
             Therefore start at -2.
     */
    for(int i = strlen(path) - 1; i >= 0; --i) {
        if(path[i] == G_DIR_SEPARATOR) {
            /* Do not use an empty path, use a slash for root */
            if(i == 0) {
                path[0] = G_DIR_SEPARATOR;
                path[1] = 0;
            } else {
                path[i] = 0;
            }

            /* Include the nulbyte */
            int new_count = -1;

            if(error_flag == false) {
                /* Lookup the count on this level */
                int old_count = GPOINTER_TO_INT(rm_trie_search(count_tree, path));

                /* Propagate old error up or just increment the count */
                new_count = (old_count == -1) ? -1 : old_count + 1;
            }

            /* Accumulate the count ('n' above is the height of the trie)  */
            rm_trie_insert(count_tree, path, GINT_TO_POINTER(new_count));
        }
    }

    return 0;
}

static bool rm_tm_count_files(RmTrie *count_tree, const RmCfg *const cfg) {
    /* put paths into format expected by fts */
    g_assert(cfg);
    guint path_count = cfg->path_count;

    g_assert(path_count);
    g_assert(path_count == g_slist_length(cfg->paths));

    const char **const path_vec = malloc(sizeof(*path_vec) * (path_count + 1));
    if(!path_vec) {
        rm_log_error(_("Failed to allocate memory. Out of memory?"));
        return false;
    }

    const char **path = path_vec + path_count; *path = 0;
    for(const GSList *paths = cfg->paths; paths; paths = paths->next) {
        g_assert(paths->data);
        *(--path) = ((RmPath *)paths->data)->path;
    } g_assert(path == path_vec);

    /* This tree stores the full file paths.
       It is joined into a full directory tree later.
     */
    RmTrie file_tree;
    rm_trie_init(&file_tree);


    FTS *fts = fts_open(path_vec, FTS_COMFOLLOW | FTS_PHYSICAL, NULL);
    if(fts == NULL) {
        rm_log_perror("fts_open failed");
        goto fail;
    }

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
        case FTS_ERR:
        case FTS_DC:
            /* Save this path as an error */
            rm_trie_insert(&file_tree, ent->fts_path, GINT_TO_POINTER(true));
            break;
        case FTS_F:
        case FTS_SL:
        case FTS_NS:
        case FTS_SLNONE:
        case FTS_DEFAULT:
            /* Save this path as countable file, but only if we consider empty files */
            if(!(cfg->find_emptyfiles) || ent->fts_statp->st_size > 0) {
                if(!(cfg->follow_symlinks && ent->fts_info == FTS_SL)) {
                    rm_trie_insert(&file_tree, ent->fts_path, GINT_TO_POINTER(false));
                }
            }
        case FTS_D:
        case FTS_DNR:
        case FTS_DOT:
        case FTS_DP:
        case FTS_NSOK:
        default:
            /* other fts states, that do not count as errors or files */
            break;
        }
    }

    if(fts_close(fts) != 0) {
        rm_log_perror("fts_close failed");
        goto fail;
    }

    rm_trie_iter(&file_tree, NULL, true, false, rm_tm_count_art_callback, count_tree);

    /* Now flag everything as a no-go over the given paths,
     * otherwise we would continue merging till / with fatal consequences,
     * since / does not have more files than path_vec[0]
     */
    for(/*path = path_vec*/; *path; ++path) {
        /* Just call the callback directly */
        RmNode *node = rm_trie_search_node(&file_tree, *path);
        if(node != NULL) {
            node->data = GINT_TO_POINTER(true);
            rm_tm_count_art_callback(&file_tree, node, 0, count_tree);
        }
    }

    rm_trie_destroy(&file_tree);
    g_free(path_vec);
    return true;

    fail:
        g_free(path_vec);
        return false;
}

///////////////////////////////
// DIRECTORY STRUCT HANDLING //
///////////////////////////////

static RmDirectory *rm_directory_new(char *dirname) {
    RmDirectory *self = g_new0(RmDirectory, 1);

    self->file_count = 0;
    self->dupe_count = 0;
    self->prefd_files = 0;
    self->was_merged = false;
    self->was_inserted = false;
    self->was_dupe_extracted = false;
    self->mergeups = 0;

    self->dirname = dirname;
    self->finished = false;

    self->depth = 0;
    for(char *s = dirname; *s; s++) {
        self->depth += (*s == G_DIR_SEPARATOR);
    }

    RmStat dir_stat;
    if(rm_sys_stat(self->dirname, &dir_stat) == -1) {
        rm_log_perror("stat(2) failed during sort");
    } else {
        self->metadata.dir_mtime = rm_sys_stat_mtime_float(&dir_stat);
        self->metadata.dir_inode = dir_stat.st_ino;
        self->metadata.dir_dev = dir_stat.st_dev;
    }

    /* Special cumulative hashsum, that is not dependent on the
     * order in which the file hashes were added.
     * It is not used as full hash, but as sorting speedup.
     */
    self->digest = rm_digest_new(RM_DIGEST_CUMULATIVE, 0);

    g_queue_init(&self->known_files);
    g_queue_init(&self->children);

    self->hash_set =
        g_hash_table_new((GHashFunc)rm_digest_hash, (GEqualFunc)rm_digest_equal);

    return self;
}

static void rm_directory_free(RmDirectory *self) {
    rm_digest_free(self->digest);
    g_hash_table_unref(self->hash_set);
    g_queue_clear(&self->known_files);
    g_queue_clear(&self->children);
    g_free(self->dirname);
    g_free(self);
}

static RmOff rm_tm_calc_file_size(const RmDirectory *directory) {
    RmOff acc = 0;

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        acc += file->actual_file_size;
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        acc += rm_tm_calc_file_size((RmDirectory *)iter->data);
    }

    return acc;
}

static void rm_directory_to_file(RmTreeMerger *merger, const RmDirectory *self,
                                 RmFile *file) {
    memset(file, 0, sizeof(RmFile));

    /* Need to set session first, since set_path expects that */
    file->session = merger->session;
    rm_file_set_path(file, self->dirname);

    file->lint_type = RM_LINT_TYPE_DUPE_DIR_CANDIDATE;
    file->digest = self->digest;

    /* Set these to invalid for now */
    file->mtime = self->metadata.dir_mtime;
    file->inode = self->metadata.dir_inode;
    file->dev = self->metadata.dir_dev;
    file->depth = rm_util_path_depth(self->dirname);

    /* Recursively calculate the file size */
    file->file_size = rm_tm_calc_file_size(self);
    file->actual_file_size = file->file_size;
    file->is_prefd = (self->prefd_files >= self->dupe_count);
}

static RmFile *rm_directory_as_new_file(RmTreeMerger *merger, const RmDirectory *self) {
    /* Masquerades an RmDirectory as RmFile for purpose of output */
    RmFile *file = g_malloc0(sizeof(RmFile));
    rm_directory_to_file(merger, self, file);
    return file;
}

static bool rm_directory_equal(RmDirectory *d1, RmDirectory *d2) {
    if(d1->dupe_count != d2->dupe_count) {
        return false;
    }

    if(rm_digest_equal(d1->digest, d2->digest) == false) {
        return false;
    }

    if(g_hash_table_size(d1->hash_set) != g_hash_table_size(d2->hash_set)) {
        return false;
    }

    /* Compare the exact contents of the hash sets */
    gpointer digest_key;
    GHashTableIter iter;

    g_hash_table_iter_init(&iter, d1->hash_set);
    while(g_hash_table_iter_next(&iter, &digest_key, NULL)) {
        if(g_hash_table_contains(d2->hash_set, digest_key) == false) {
            return false;
        }
    }

    return true;
}

static guint rm_directory_hash(const RmDirectory *d) {
    /* This hash is used to quickly compare directories with each other.
     * Different directories might yield the same hash of course.
     * To prevent this case, rm_directory_equal really compares
     * all the file's hashes with each other.
     */
    return rm_digest_hash(d->digest) ^ d->dupe_count;
}

static void rm_directory_add(RmTreeMerger *self, RmDirectory *directory, RmFile *file) {
    g_assert(file);
    g_assert(file->digest);
    g_assert(directory);

    guint8 *file_digest = NULL;
    RmOff digest_bytes = 0;

    file_digest = rm_digest_steal(file->digest);
    digest_bytes = file->digest->bytes;

    /* Update the directorie's hash with the file's hash
       Since we cannot be sure in which order the files come in
       we have to add the hash cummulatively.
     */
    rm_digest_update(directory->digest, file_digest, digest_bytes);

    /* Add the path to the checksum if we require the same layout too */
    if(self->session->cfg->honour_dir_layout) {
        const char *basename = file->folder->basename;
        gsize basename_cksum_len = 0;
        guint8 *basename_cksum = rm_digest_sum(
                RM_DIGEST_BLAKE2B,
                (const guint8 *)basename,
                strlen(basename) + 1,  /* include the nul-byte */
                &basename_cksum_len
        );
        rm_digest_update(directory->digest, basename_cksum, basename_cksum_len);
        g_slice_free1(basename_cksum_len, basename_cksum);
    }

    g_slice_free1(digest_bytes, file_digest);

    /* The file value is not really used, but we need some non-null value */
    g_hash_table_add(directory->hash_set, file->digest);

    directory->dupe_count += 1;
    directory->prefd_files += file->is_prefd;
}

static void rm_directory_add_subdir(RmTreeMerger *self, RmDirectory *parent, RmDirectory *subdir) {
    if(subdir->was_merged) {
        return;
    }

    parent->mergeups = subdir->mergeups + parent->mergeups + 1;
    parent->dupe_count += subdir->dupe_count;
    g_queue_push_head(&parent->children, subdir);
    parent->prefd_files += subdir->prefd_files;

#ifdef _RM_TREEMERGE_DEBUG
    g_printerr("%55s (%3ld/%3ld) <- %s (%3ld/%3ld)\n", parent->dirname,
               parent->dupe_count, parent->file_count, subdir->dirname,
               subdir->dupe_count, subdir->file_count);
#endif

    /* Take over the child's digests */
    for(GList *iter = subdir->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        g_hash_table_add(parent->hash_set, file->digest);
    }

    /* Inherit the child's checksum */
    unsigned char *subdir_cksum = rm_digest_steal(subdir->digest);
    rm_digest_update(parent->digest, subdir_cksum, subdir->digest->bytes);
    g_slice_free1(subdir->digest->bytes, subdir_cksum);

    if(self->session->cfg->honour_dir_layout) {
        char *basename = g_path_get_basename(subdir->dirname);
        gsize basename_cksum_len = 0;
        guint8* basename_cksum = rm_digest_sum(
                RM_DIGEST_BLAKE2B,
                (const guint8 *)basename,
                strlen(basename) + 1,  /* include the nul-byte */
                &basename_cksum_len
        );

        rm_digest_update(parent->digest, basename_cksum, basename_cksum_len);
        g_slice_free1(basename_cksum_len, basename_cksum);
        g_free(basename);
    }

    subdir->was_merged = true;
}

///////////////////////////
// TREE MERGER ALGORITHM //
///////////////////////////

RmTreeMerger *rm_tm_new(RmSession *session) {
    RmTreeMerger *self = g_slice_new(RmTreeMerger);
    self->session = session;
    self->callback = NULL;
    self->callback_data = NULL;
    self->free_map = g_hash_table_new(NULL, NULL);
    g_queue_init(&self->valid_dirs);

    self->result_table = g_hash_table_new_full((GHashFunc)rm_directory_hash,
                                               (GEqualFunc)rm_directory_equal, NULL,
                                               (GDestroyNotify)g_queue_free);

    self->file_groups =
        g_hash_table_new_full((GHashFunc)rm_digest_hash, (GEqualFunc)rm_digest_equal,
                              NULL, (GDestroyNotify)g_queue_free);

    self->known_hashs = g_hash_table_new_full(NULL, NULL, NULL, NULL);

    rm_trie_init(&self->dir_tree);
    rm_trie_init(&self->count_tree);

    if(!rm_tm_count_files(&self->count_tree, session->cfg)) {
        return 0;
    }

    return self;
}

void rm_tm_set_callback(RmTreeMerger *self, RmTreeMergeOutputFunc callback, gpointer data) {
    g_assert(self);
    self->callback = callback;
    self->callback_data = data;
}

int rm_tm_destroy_iter(_UNUSED RmTrie *self, RmNode *node, _UNUSED int level,
                       _UNUSED RmTreeMerger *tm) {
    RmDirectory *directory = node->data;
    rm_directory_free(directory);
    return 0;
}

static void rm_tm_insert_dir(RmTreeMerger *self, RmDirectory *directory) {
    if(directory->was_inserted) {
        return;
    }

    GQueue *dir_queue =
        rm_hash_table_setdefault(self->result_table, directory, (RmNewFunc)g_queue_new);
    g_queue_push_head(dir_queue, directory);
    directory->was_inserted = true;
}

void rm_tm_feed(RmTreeMerger *self, RmFile *file) {
    g_assert(self);
    g_assert(file);

    RM_DEFINE_PATH(file);
    char *dirname = g_path_get_dirname(file_path);

    /* See if we know that directory already */
    RmDirectory *directory = rm_trie_search(&self->dir_tree, dirname);

    if(directory == NULL) {
        /* Get the actual file count */
        int file_count = GPOINTER_TO_INT(rm_trie_search(&self->count_tree, dirname));
        if(file_count == 0) {
            rm_log_error(
                RED "Empty directory or weird RmFile encountered; rejecting.\n" RESET);
            file_count = -1;
        }

        directory = rm_directory_new(dirname);
        directory->file_count = file_count;

        /* Make the new directory known */
        rm_trie_insert(&self->dir_tree, dirname, directory);

        g_queue_push_head(&self->valid_dirs, directory);
    } else {
        g_free(dirname);
    }

    g_hash_table_insert(self->free_map, file, file);
    rm_directory_add(self, directory, file);

    /* Add the file to this directory */
    g_queue_push_head(&directory->known_files, file);

    /* Remember the digest (if only to free it later...) */
    g_hash_table_replace(self->known_hashs, file->digest, NULL);

    /* Check if the directory reached the number of actual files in it */
    if(directory->dupe_count == directory->file_count && directory->file_count > 0) {
        rm_tm_insert_dir(self, directory);
    }
}

static void rm_tm_mark_finished(RmTreeMerger *self, RmDirectory *directory) {
    if(directory->finished) {
        return;
    }

    directory->finished = true;

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        rm_tm_mark_finished(self, (RmDirectory *)iter->data);
    }
}

static void rm_tm_mark_original_files(RmTreeMerger *self, RmDirectory *directory) {
    directory->finished = false;

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        RmDirectory *child = iter->data;
        rm_tm_mark_original_files(self, child);
    }
}

static gint64 rm_tm_mark_duplicate_files(RmTreeMerger *self, RmDirectory *directory) {
    gint64 acc = 0;

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        acc += file->is_prefd;
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        RmDirectory *child = iter->data;
        acc += rm_tm_mark_duplicate_files(self, child);
    }

    return acc;
}

static void rm_tm_output_file(RmTreeMerger *self, RmFile *file) {
    g_assert(self->callback);
    self->callback(file, self->callback_data);
    g_hash_table_remove(self->free_map, file);
}

static void rm_tm_write_unfinished_cksums(RmTreeMerger *self, RmDirectory *directory) {
    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        file->lint_type = RM_LINT_TYPE_UNIQUE_FILE;
        file->twin_count = -1;
        rm_tm_output_file(self, file);
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        RmDirectory *child = iter->data;
        rm_tm_write_unfinished_cksums(self, child);
    }
}

static int rm_tm_sort_paths(const RmDirectory *da, const RmDirectory *db,
                            _UNUSED RmTreeMerger *self) {
    return da->depth - db->depth;
}

static int rm_tm_sort_paths_reverse(const RmDirectory *da, const RmDirectory *db,
                                    _UNUSED RmTreeMerger *self) {
    return -rm_tm_sort_paths(da, db, self);
}

static int rm_tm_sort_orig_criteria(const RmDirectory *da, const RmDirectory *db,
                                    RmTreeMerger *self) {
    RmCfg *cfg = self->session->cfg;

    if(da->prefd_files - db->prefd_files) {
        if(cfg->keep_all_untagged) {
            return da->prefd_files - db->prefd_files;
        } else {
            return db->prefd_files - da->prefd_files;
        }
    }

    RmFile file_a, file_b;
    rm_directory_to_file(self, da, &file_a);
    rm_directory_to_file(self, db, &file_b);

    return rm_pp_cmp_orig_criteria(&file_a, &file_b, self->session);
}

static void rm_tm_forward_unresolved(RmTreeMerger *self, RmDirectory *directory) {
    if(directory->finished == true) {
        return;
    } else {
        directory->finished = true;
    }

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        GQueue *file_list = rm_hash_table_setdefault(
            self->file_groups,
            file->digest,
            (RmNewFunc)g_queue_new
        );

        g_queue_push_head(file_list, file);
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        rm_tm_forward_unresolved(self, (RmDirectory *)iter->data);
    }
}

static int rm_tm_iter_unfinished_files(_UNUSED RmTrie *trie, RmNode *node,
                                       _UNUSED int level, _UNUSED void *user_data) {
    RmTreeMerger *self = user_data;
    rm_tm_forward_unresolved(self, node->data);
    return 0;
}

static int rm_tm_cmp_directory_groups(GQueue *a, GQueue *b) {
    if(a->length == 0 || b->length == 0) {
        return b->length - a->length;
    }

    RmDirectory *first_a = a->head->data;
    RmDirectory *first_b = b->head->data;
    return first_b->mergeups - first_a->mergeups;
}

static int rm_tm_hidden_file(RmFile *file, _UNUSED gpointer user_data) {
    RM_DEFINE_PATH(file);
    return file->is_hidden;
}

static void rm_tm_filter_hidden_directories(GQueue *directories) {
    GQueue *kill_list = g_queue_new();

    for(GList *iter = directories->head; iter; iter = iter->next) {
        RmDirectory *directory = iter->data;
        if(!g_str_has_prefix(rm_util_basename(directory->dirname), ".")) {
            continue;
        }

        g_queue_push_tail(kill_list, directory);
    }

    // Actually remove the directories:
    // (If the directory list is only one directory long now,
    //  it is being filtered later in the process)
    for(GList *iter = kill_list->head; iter; iter = iter->next) {
        g_queue_remove(directories, iter->data);
    }

    g_queue_free(kill_list);
}

static void rm_tm_extract_part_of_dir_dupes(RmTreeMerger *self, RmDirectory *directory) {
    if(directory->was_dupe_extracted) {
        return;
    }

    directory->was_dupe_extracted = true;

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        /* Forward the part_of_directory to the output formatter.
         * We need a copy, because the type and parent_dir changes.
         */
        RmFile *file = iter->data;
        RmFile *copy = rm_file_copy(file);

        copy->parent_dir = directory;
        copy->lint_type = RM_LINT_TYPE_PART_OF_DIRECTORY;
        copy->twin_count = -1;

        rm_tm_output_file(self, copy);
    }

    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        RmDirectory *child_directory = iter->data;
        rm_tm_extract_part_of_dir_dupes(self, child_directory);
    }
}

static void rm_tm_output_group(RmTreeMerger *self, GQueue *group) {
    g_assert(self);
    g_assert(self->session);
    g_assert(group);

    if(group->length < 2) {
        return;
    }

    bool has_duplicates = false;

    for(GList *iter = group->head; iter; iter=iter->next) {
        RmFile *file = iter->data;
        if(!file->is_original) {
            has_duplicates = true;
        }
    }

    if(!has_duplicates) {
        return;
    }

    for(GList *iter = group->head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        file->twin_count = group->length;
        rm_tm_output_file(self, file);
    }
}

static void rm_tm_extract(RmTreeMerger *self) {
    /* Iterate over all directories per hash (which are same therefore) */
    RmCfg *cfg = self->session->cfg;
    GList *result_table_values = g_hash_table_get_values(self->result_table);
    result_table_values =
        g_list_sort(result_table_values, (GCompareFunc)rm_tm_cmp_directory_groups);

    for(GList *iter = result_table_values; iter; iter = iter->next) {
        /* Needs at least two directories to be duplicate... */
        GQueue *dir_list = iter->data;

#ifdef _RM_TREEMERGE_DEBUG
        for(GList *i = dir_list->head; i; i = i->next) {
            RmDirectory *d = i->data;
            char buf[512];
            memset(buf, 0, sizeof(buf));
            rm_digest_hexstring(d->digest, buf);
            g_printerr("    mergeups=%" LLU ": %s - %s\n", d->mergeups, d->dirname, buf);
        }
        g_printerr("---\n");
#endif
        if(dir_list->length < 2) {
            continue;
        }

        if(rm_session_was_aborted()) {
            break;
        }

        /* List of result directories */
        GQueue result_dirs = G_QUEUE_INIT;

        /* Sort the RmDirectory list by their path depth, lowest depth first */
        g_queue_sort(dir_list, (GCompareDataFunc)rm_tm_sort_paths, self);

        /* If no --hidden is given, do not display top-level directories
         * that are hidden. If needed, filter them beforehand. */
        if(cfg->partial_hidden) {
            rm_tm_filter_hidden_directories(dir_list);
        }

        /* Output the directories and mark their children to prevent
         * duplicate directory reports in lower levels.
         */
        for(GList *iter = dir_list->head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            if(directory->finished == false) {
                rm_tm_mark_finished(self, directory);
                g_queue_push_head(&result_dirs, directory);
            }
        }

        /* Make sure the original directory lands as first
         * in the result_dirs queue. Also convert it from RmDirectory
         * to a fake RmFile, so the output module can handle it.
         */
        g_queue_sort(&result_dirs, (GCompareDataFunc)rm_tm_sort_orig_criteria, self);

        GQueue file_adaptor_group = G_QUEUE_INIT;

        for(GList *iter = result_dirs.head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            rm_tm_extract_part_of_dir_dupes(self, directory);

            RmFile *mask = rm_directory_as_new_file(self, directory);
            g_queue_push_tail(&file_adaptor_group, mask);
            g_hash_table_insert(self->free_map, mask, mask);

            if(iter == result_dirs.head) {
                /* First one in the group -> It's the original */
                mask->is_original = true;
                rm_tm_mark_original_files(self, directory);
            } else {
                gint64 prefd = rm_tm_mark_duplicate_files(self, directory);
                if(prefd == directory->dupe_count && cfg->keep_all_tagged) {
                    /* Mark the file as original when all files in it are preferred. */
                    mask->is_original = true;
                } else if(prefd == 0 && cfg->keep_all_untagged) {
                    mask->is_original = true;
                }
            }

            if(self->session->cfg->write_unfinished) {
                rm_tm_write_unfinished_cksums(self, directory);
            }

        }

        rm_tm_output_group(self, &file_adaptor_group);

        g_queue_clear(&file_adaptor_group);
        g_queue_clear(&result_dirs);
    }

    g_list_free(result_table_values);

    /* Iterate over all non-finished dirs in the tree,
     * and grab unfinished files that must be dupes elsewhise.
     */
    rm_trie_iter(&self->dir_tree, NULL, true, false, rm_tm_iter_unfinished_files, self);

    /* Now here's a problem. Consider an input like this:
     *  /root
     *  ├── a
     *  ├── sub1
     *  │   ├── a
     *  │   └── b
     *  └── sub2
     *      ├── a
     *      └── b
     *
     *  This yields two duplicate dirs (sub1, sub2)
     *  and one duplicate, unmatched file (a).
     *
     *  For outputting files we need groups, which consist of at least 2 files.
     *  So how to group that, so we don't end up deleting a file many times?
     *  We always choose which directories are originals first, so we flag all
     *  files in it as originals.
     */
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, self->file_groups);

    GQueue *file_list = NULL;
    while(g_hash_table_iter_next(&iter, NULL, (void **)&file_list)) {
        if(self->session->cfg->partial_hidden) {
            /* with --partial-hidden we do not want to output left overs that are hidden */
            rm_util_queue_foreach_remove(file_list, (RmRFunc)rm_tm_hidden_file, NULL);
        }

        if(file_list->length >= 2) {
            /* If no separate duplicate files are requested, we can stop here */
            if(self->session->cfg->find_duplicates == false) {
                self->session->dup_group_counter -= 1;
                self->session->dup_counter -= file_list->length - 1;
            } else {
                rm_shred_group_find_original(self->session, file_list,
                                             RM_SHRED_GROUP_FINISHING);
                rm_tm_output_group(self, file_list);
            }
        }
    }
}

static void rm_tm_cluster_up(RmTreeMerger *self, RmDirectory *directory) {
    char *parent_dir = g_path_get_dirname(directory->dirname);
    bool is_root = strcmp(parent_dir, "/") == 0;

    /* Lookup if we already found this parent before (if yes, merge with it) */
    RmDirectory *parent = rm_trie_search(&self->dir_tree, parent_dir);

    if(parent == NULL) {
        /* none yet, basically copy child */
        parent = rm_directory_new(parent_dir);
        rm_trie_insert(&self->dir_tree, parent_dir, parent);

        /* Get the actual file count */
        parent->file_count =
            GPOINTER_TO_UINT(rm_trie_search(&self->count_tree, parent_dir));

    } else {
        g_free(parent_dir);
    }

    rm_directory_add_subdir(self, parent, directory);

    if(parent->dupe_count == parent->file_count && parent->file_count > 0) {
        rm_tm_insert_dir(self, parent);
        if(!is_root) {
            rm_tm_cluster_up(self, parent);
        }
    }
}

void rm_tm_finish(RmTreeMerger *self) {
    g_assert(self);
    g_assert(self->callback);

    /* Iterate over all valid directories and try to level them all layers up.
     */
    g_queue_sort(&self->valid_dirs, (GCompareDataFunc)rm_tm_sort_paths_reverse, self);
    for(GList *iter = self->valid_dirs.head; iter; iter = iter->next) {
        RmDirectory *directory = iter->data;
        rm_tm_cluster_up(self, directory);
#ifdef _RM_TREEMERGE_DEBUG
        g_printerr("###\n");
#endif
    }

    if(!rm_session_was_aborted()) {
        /* Recursively call self to march on */
        rm_tm_extract(self);
    }
}

void rm_tm_destroy(RmTreeMerger *self) {
    g_hash_table_unref(self->result_table);
    g_hash_table_unref(self->file_groups);

    // TODO: What was that supposed to do?
    // GList *digest_keys = g_hash_table_get_keys(self->known_hashs);
    // g_list_free_full(digest_keys, (GDestroyNotify)rm_digest_free);
    g_hash_table_unref(self->known_hashs);

    g_queue_clear(&self->valid_dirs);

    /* Kill all RmDirectories stored in the tree */
    rm_trie_iter(&self->dir_tree, NULL, true, false,
                 (RmTrieIterCallback)rm_tm_destroy_iter, self);

    rm_trie_destroy(&self->dir_tree);
    rm_trie_destroy(&self->count_tree);

    /*  Iterate over all files that were not forwarded to the
     *  output module (where they would be freed)
     *  */
    GList *file_list = g_hash_table_get_values(self->free_map);
    g_list_free_full(file_list, (GDestroyNotify)rm_file_destroy);
    g_hash_table_unref(self->free_map);

    g_slice_free(RmTreeMerger, self);
}

