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

/* This is the treemerge algorithm.
 *
 * It tries to solve the following problem and sometimes even succeeds:
 * Take a list of duplicates (as RmFiles) and figure out which directories
 * consist fully out of duplicates and can be thus removed.
 *
 * The basic algorithm is split in four phases:
 *
 * - Counting:  Walk through all directories given on the commandline and
 *              traverse them. Count all files during traverse and safe it in
 *              an radix-tree (libart is used here). The key is the path, the
 *              value the count of files in it. Invalid directories and
 *              directories above the given are set to -1.
 * - Feeding:   Collect all duplicates and store them in RmDirectory structures.
 *              If a directory appears to consist of dupes only (num_dupes == num_files)
 *              then it is remembered as valid directory.
 * - Upcluster: Take all valid directories and cluster them up, so subdirs get
 *              merged into the parent directory. Continue as long the parent
 *              directory is full too. Remember full directories in a hashtable
 *              with the hash of the directory (which is a hash of the file's
 *              hashes) as key and a list of matching directories as value.
 * - Extract:   Extract the result information out of the hashtable top-down.
 *              If a directory is reported, mark all subdirs of it as finished
 *              so they do not get reported twice. Files that could not be
 *              grouped in directories are found and reported as usually.
 */

/*
 * Comment this out to see helpful extra debugging:
 */
// #define _RM_TREEMERGE_DEBUG

#include <glib.h>
#include <string.h>
#include <fts.h>

#include "treemerge.h"
#include "shredder.h"
#include "preprocess.h"
#include "formats.h"
#include "pathtricia.h"

/* patricia trie implementation */
#include "libart/art.h"


typedef struct RmDirectory {
    char *dirname;        /* Path to this directory without trailing slash              */
    GQueue known_files;   /* RmFiles in this directory                                  */
    GQueue children;      /* Children for directories with subdirectories               */
    gint64 prefd_files;   /* Files in this directory that are tagged as original        */
    gint64 dupe_count;    /* Count of RmFiles actually in this directory                */
    gint64 file_count;    /* Count of files actually in this directory (or -1 on error) */
    gint64 mergeups;      /* number of times this directory was merged up               */
    bool finished;        /* Was this dir or one of his parents already printed?        */
    bool was_merged;      /* true if this directory was merged up already (only once)   */
    bool was_inserted;    /* true if this directory was added to results (only once)    */
    unsigned short depth; /* path depth (i.e. count of / in path, no trailing /)        */
    art_tree hash_trie;   /* Trie of hashes, used for equality check (to be sure)       */
    RmDigest *digest;     /* Common digest of all RmFiles in this directory             */

    struct {
        bool has_metadata; /* stat(2) called already                */
        time_t dir_mtime;  /* Directory Metadata: Modification Time */
        ino_t dir_inode;   /* Directory Metadata: Inode             */
        dev_t dir_dev;     /* Directory Metadata: Device ID         */
    } metadata;
} RmDirectory;

struct RmTreeMerger {
    RmSession *session;       /* Session state variables / Settings          */
    RmTrie dir_tree;          /* Path-Trie with all RmFiles as value         */
    RmTrie count_tree;        /* Path-Trie with all file's count as value    */
    GHashTable *result_table; /* {hash => [RmDirectory]} mapping             */
    GHashTable *file_groups;  /* Group files by hash                         */
    GHashTable *file_checks;  /* Set of files that were handled already.     */
    GHashTable *known_hashs;  /* Set of known hashes, only used for cleanup. */
    GQueue valid_dirs;        /* Directories consisting of RmFiles only      */
};

//////////////////////////
// ACTUAL FILE COUNTING //
//////////////////////////

int rm_tm_count_art_callback(_U RmTrie *self, RmNode *node, _U int level, void *user_data) {
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
    rm_trie_build_path(node, path, sizeof(path)); 

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
                int old_count =
                    GPOINTER_TO_INT(rm_trie_search(count_tree, path));

                /* Propagate old error up or just increment the count */
                new_count = (old_count == -1) ? -1 : old_count + 1;
            }

            /* Accumulate the count ('n' above is the height of the trie)  */
            rm_trie_insert(count_tree, path, GINT_TO_POINTER(new_count));
        }
    }

    return 0;
}

static bool rm_tm_count_files(RmTrie *count_tree, char **paths, RmSession *session) {
    if(*paths == NULL) {
        rm_log_error("No paths passed to rm_tm_count_files\n");
        return false;
    }

    int fts_flags = FTS_COMFOLLOW;
    if(session->cfg->follow_symlinks) {
        fts_flags |= FTS_LOGICAL;
    } else {
        fts_flags |= FTS_PHYSICAL;
    }

    /* This tree stores the full file paths.
       It is joined into a full directory tree later.
     */
    RmTrie file_tree;
    rm_trie_init(&file_tree);

    FTS *fts = fts_open(paths, fts_flags, NULL);
    if(fts == NULL) {
        rm_log_perror("fts_open failed");
        return false;
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
            /* Save this path as countable file */
            if(ent->fts_statp->st_size > 0) {
                rm_trie_insert(&file_tree, ent->fts_path, GINT_TO_POINTER(false));
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
        return false;
    }

    rm_trie_iter(&file_tree, NULL, true, false, rm_tm_count_art_callback, count_tree);

    /* Now flag everything as a no-go over the given paths,
     * otherwise we would continue merging till / with fatal consequences,
     * since / does not have more files as paths[0]
     */
    for(int i = 0; paths[i]; ++i) {
        /* Just call the callback directly */
        RmNode *node = rm_trie_search_node(&file_tree, paths[i]); 
        node->data = GINT_TO_POINTER(true);
        rm_tm_count_art_callback(&file_tree, node, 0, count_tree);
    }

#ifdef _RM_TREEMERGE_DEBUG
    rm_trie_print(count_tree);
#endif

    rm_trie_destroy(&file_tree);
    return true;
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
        self->metadata.dir_mtime = dir_stat.st_mtime;
        self->metadata.dir_inode = dir_stat.st_ino;
        self->metadata.dir_dev = dir_stat.st_dev;
    }

    /* Special cumulative hashsum, that is not dependent on the
     * order in which the file hashes were added.
     * It is not used as full hash, but as sorting speedup.
     */
    self->digest = rm_digest_new(RM_DIGEST_CUMULATIVE, 0, 0, 0);

    g_queue_init(&self->known_files);
    g_queue_init(&self->children);

    init_art_tree(&self->hash_trie);

    return self;
}

static void rm_directory_free(RmDirectory *self) {
    rm_digest_free(self->digest);
    destroy_art_tree(&self->hash_trie);
    g_queue_clear(&self->known_files);
    g_queue_clear(&self->children);
    g_free(self->dirname);
    g_free(self);
}

static RmOff rm_tm_calc_file_size(RmDirectory *directory) {
    RmOff acc = 0;

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        acc += file->file_size;
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        acc += rm_tm_calc_file_size((RmDirectory *)iter->data);
    }

    return acc;
}

static RmFile *rm_directory_as_file(RmTreeMerger *merger, RmDirectory *self) {
    /* Masquerades a RmDirectory as RmFile for purpose of output */
    RmFile *file = g_malloc0(sizeof(RmFile));

    /* Need to set session first, since set_path expects that */
    file->session = merger->session;
    rm_file_set_path(file, self->dirname, strlen(self->dirname), FALSE);

    file->lint_type = RM_LINT_TYPE_DUPE_DIR_CANDIDATE;
    file->digest = self->digest;

    /* Set these to invalid for now */
    file->mtime = self->metadata.dir_mtime;
    file->inode = self->metadata.dir_inode;
    file->dev = self->metadata.dir_dev;

    /* Recursively calculate the file size */
    file->file_size = rm_tm_calc_file_size(self);
    file->is_prefd = (self->prefd_files >= self->dupe_count);

    return file;
}

static int rm_directory_equal_iter(art_tree *other_hash_trie, const unsigned char *key,
                                   uint32_t key_len, _U void *value) {
    return !GPOINTER_TO_UINT(art_search(other_hash_trie, (unsigned char *)key, key_len));
}

static bool rm_directory_equal(RmDirectory *d1, RmDirectory *d2) {
    /* Will this work with paranoid cfg? Probably, but in a weird way.
     * Also it might not be very secure when the last block of the file is
     * compared...
     * */
    if(d1->mergeups != d2->mergeups) {
        return false;
    }

    if(rm_digest_equal(d1->digest, d2->digest) == false) {
        return false;
    }

    if(art_size(&d1->hash_trie) != art_size(&d2->hash_trie)) {
        return false;
    }

    /* Take the bitter pill and compare all hashes manually.
     * This should only happen on hash collisions of ->digest.
     */
    return !art_iter(&d1->hash_trie, (art_callback)rm_directory_equal_iter,
                     &d2->hash_trie);
}

static guint rm_directory_hash(const RmDirectory *d) {
    /* This hash is used to quickly compare directories with each other.
     * Different directories might yield the same hash of course.
     * To prevent this case, rm_directory_equal really compares
     * all the file's hashes with each other.
     */
    return rm_digest_hash(d->digest) ^ d->mergeups;
}

static int rm_directory_add(RmDirectory *directory, RmFile *file) {
    /* Update the directorie's hash with the file's hash
       Since we cannot be sure in which order the files come in
       we have to add the hash cummulatively.
     */
    int new_dupes = 0;

    g_assert(file);
    g_assert(file->digest);
    g_assert(directory);

    guint8 *file_digest = NULL;
    RmOff digest_bytes = 0;

    if(file->digest->type == RM_DIGEST_PARANOID) {
        file_digest = rm_digest_steal_buffer(file->digest->shadow_hash);
        digest_bytes = file->digest->shadow_hash->bytes;
    } else {
        file_digest = rm_digest_steal_buffer(file->digest);
        digest_bytes = file->digest->bytes;
    }

    /* + and not XOR, since ^ would yield 0 for same hashes always. No matter
     * which hashes. Also this would be confusing. For me and for debuggers.
     */
    rm_digest_update(directory->digest, file_digest, digest_bytes);

    /* The file value is not really used, but we need some non-null value */
    art_insert(&directory->hash_trie, file_digest, digest_bytes, file);
    g_slice_free1(digest_bytes, file_digest);

    if(file->hardlinks.is_head && file->hardlinks.files) {
        new_dupes = 1 + g_queue_get_length(file->hardlinks.files);
    } else {
        new_dupes = 1;
    }

    directory->dupe_count += new_dupes;
    directory->prefd_files += file->is_prefd;

    return new_dupes;
}

static void rm_directory_add_subdir(RmDirectory *parent, RmDirectory *subdir) {
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

    /**
     * Here's something weird:
     * - a counter is used and substraced at once from parent->dupe_count.
     * - it would ofc. be nicer to substract it step by step.
     * - but for some weird reasons this only works on clang, not gcc.
     * - yes, what. But I tested this, I promise!
     */
    for(GList *iter = subdir->known_files.head; iter; iter = iter->next) {
        int c = rm_directory_add(parent, (RmFile *)iter->data);
        parent->dupe_count -= c;
    }

    /* Inherit the child's checksum */
    unsigned char *subdir_cksum = rm_digest_steal_buffer(subdir->digest);
    rm_digest_update(parent->digest, subdir_cksum, subdir->digest->bytes);
    g_slice_free1(subdir->digest->bytes, subdir_cksum);

    subdir->was_merged = true;
}

///////////////////////////
// TREE MERGER ALGORITHM //
///////////////////////////

static void rm_tm_chunk_flush(RmTreeMerger *self, char **out_paths, int n_paths) {
    rm_tm_count_files(&self->count_tree, out_paths, self->session);

    if(self->session->cfg->use_meta_cache) {
        for(int i = 0; i < n_paths; ++i) {
            g_free(out_paths[i]);
        }
    }
}

static void rm_tm_chunk_paths(RmTreeMerger *self, char **paths) {
    /* Count only up to 512 paths at the same time. High numbers like this can
     * happen if find is piped inside rmlint via the special "-" file.
     * Sadly, this would need to have all paths in memory at the same time.
     * With session->cfg->use_meta_cache, there is only an ID in the path
     * pointer.
     * */

    RmCfg *cfg = self->session->cfg;

    const int N = 512;

    int n_paths = 0;
    char **out_paths = g_malloc0((N + 1) * sizeof(char *));

    for(int i = 0; paths[i]; ++i) {
        if(cfg->use_meta_cache) {
            char buf[PATH_MAX];

            rm_swap_table_lookup(self->session->meta_cache,
                                 self->session->meta_cache_dir_id,
                                 GPOINTER_TO_UINT(paths[i]), buf, sizeof(buf));

            out_paths[n_paths] = g_strdup(buf);
        } else {
            out_paths[n_paths] = paths[i];
        }

        /* Terminate the vector by a guarding NULL */
        out_paths[++n_paths] = NULL;

        /* We reached the size of one chunk, flush and wrap around */
        if(n_paths == N) {
            rm_tm_chunk_flush(self, out_paths, n_paths);
            n_paths = 0;
        }
    }

    /* Flush the rest of it */
    if(n_paths) {
        rm_tm_chunk_flush(self, out_paths, n_paths);
    }

    g_free(out_paths);
}

RmTreeMerger *rm_tm_new(RmSession *session) {
    RmTreeMerger *self = g_slice_new(RmTreeMerger);
    self->session = session;
    g_queue_init(&self->valid_dirs);

    self->result_table = g_hash_table_new_full((GHashFunc)rm_directory_hash,
                                               (GEqualFunc)rm_directory_equal, NULL,
                                               (GDestroyNotify)g_queue_free);

    self->file_groups =
        g_hash_table_new_full((GHashFunc)rm_digest_hash, (GEqualFunc)rm_digest_equal,
                              NULL, (GDestroyNotify)g_queue_free);

    self->file_checks = g_hash_table_new_full((GHashFunc)rm_digest_hash,
                                              (GEqualFunc)rm_digest_equal, NULL, NULL);

    self->known_hashs = g_hash_table_new_full(NULL, NULL, NULL, NULL);

    rm_trie_init(&self->dir_tree);
    rm_trie_init(&self->count_tree);

    rm_tm_chunk_paths(self, session->cfg->paths);

    return self;
}

int rm_tm_destroy_iter(_U RmTrie *self, RmNode *node, _U int level, _U void *user_data) {
    RmDirectory *directory = node->data;

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        rm_file_destroy((RmFile *)iter->data);
    }

    rm_directory_free(directory);
    return 0;
}

void rm_tm_destroy(RmTreeMerger *self) {
    g_hash_table_unref(self->result_table);
    g_hash_table_unref(self->file_groups);
    g_hash_table_unref(self->file_checks);

    GList *digest_keys = g_hash_table_get_keys(self->known_hashs);
    g_list_free_full(digest_keys, (GDestroyNotify)rm_digest_free);
    g_hash_table_unref(self->known_hashs);

    g_queue_clear(&self->valid_dirs);

    /* Kill all RmDirectories stored in the tree */
    rm_trie_iter(&self->dir_tree, NULL, true, false, rm_tm_destroy_iter, NULL);
    rm_trie_destroy(&self->dir_tree);
    rm_trie_destroy(&self->count_tree);
    g_slice_free(RmTreeMerger, self);
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
    RM_DEFINE_PATH(file);
    char *dirname = g_path_get_dirname(file_path);

    /* See if we know that directory already */
    RmDirectory *directory =
        rm_trie_search(&self->dir_tree, dirname);

    if(directory == NULL) {
        /* Get the actual file count */
        int file_count = GPOINTER_TO_INT(
            rm_trie_search(&self->count_tree, dirname));
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

    rm_directory_add(directory, file);

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

static gint64 rm_tm_mark_duplicate_files(RmTreeMerger *self, RmDirectory *directory,
                                         gint64 acc) {
    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        acc += file->is_prefd;
        g_hash_table_insert(self->file_checks, file->digest, file);
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        RmDirectory *child = iter->data;
        rm_tm_mark_duplicate_files(self, child, acc);
    }

    return acc;
}

static void rm_tm_write_unfinished_cksums(RmTreeMerger *self, RmDirectory *directory) {
    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;
        RmLintType actual_type = file->lint_type;

        file->lint_type = RM_LINT_TYPE_UNFINISHED_CKSUM;
        rm_fmt_write(file, self->session->formats);
        file->lint_type = actual_type;
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        RmDirectory *child = iter->data;
        rm_tm_write_unfinished_cksums(self, child);
    }
}

static int rm_tm_sort_paths(const RmDirectory *da, const RmDirectory *db,
                            _U RmTreeMerger *self) {
    return da->depth - db->depth;
}
static int rm_tm_sort_paths_reverse(const RmDirectory *da, const RmDirectory *db,
                                    _U RmTreeMerger *self) {
    return -rm_tm_sort_paths(da, db, self);
}

static int rm_tm_sort_orig_criteria(const RmDirectory *da, const RmDirectory *db,
                                    RmTreeMerger *self) {
    if(db->prefd_files - da->prefd_files) {
        return db->prefd_files - da->prefd_files;
    }

    return rm_pp_cmp_orig_criteria_impl(
        self->session, da->metadata.dir_mtime, db->metadata.dir_mtime,
        rm_util_basename(da->dirname), rm_util_basename(db->dirname), 0, 0);
}

static void rm_tm_forward_unresolved(RmTreeMerger *self, RmDirectory *directory) {
    if(directory->finished == true) {
        return;
    } else {
        directory->finished = true;
    }

    for(GList *iter = directory->known_files.head; iter; iter = iter->next) {
        RmFile *file = iter->data;

        GQueue *file_list = rm_hash_table_setdefault(self->file_groups, file->digest,
                                                     (RmNewFunc)g_queue_new);
        g_queue_push_head(file_list, file);
    }

    /* Recursively propagate to children */
    for(GList *iter = directory->children.head; iter; iter = iter->next) {
        rm_tm_forward_unresolved(self, (RmDirectory *)iter->data);
    }
}

static int rm_tm_iter_unfinished_files(_U RmTrie *trie, RmNode *node, _U int level, _U void *user_data) {
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

static void rm_tm_extract(RmTreeMerger *self) {
    /* Iterate over all directories per hash (which are same therefore) */
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

        if(rm_session_was_aborted(self->session)) {
            break;
        }

        /* List of result directories */
        GQueue result_dirs = G_QUEUE_INIT;

        /* Sort the RmDirectory list by their path depth, lowest depth first */
        g_queue_sort(dir_list, (GCompareDataFunc)rm_tm_sort_paths, self);

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
         * in the result_dirs queue.
         */
        g_queue_sort(&result_dirs, (GCompareDataFunc)rm_tm_sort_orig_criteria, self);

        GQueue file_adaptor_group = G_QUEUE_INIT;

        for(GList *iter = result_dirs.head; iter; iter = iter->next) {
            RmDirectory *directory = iter->data;
            RmFile *mask = rm_directory_as_file(self, directory);
            g_queue_push_tail(&file_adaptor_group, mask);

            if(iter == result_dirs.head) {
                /* First one in the group -> It's the original */
                mask->is_original = true;
                rm_tm_mark_original_files(self, directory);
            } else {
                if(rm_tm_mark_duplicate_files(self, directory, 0) ==
                   directory->dupe_count) {
                    /* Mark the file as original when all files in it are preferred. */
                    mask->is_original = true;
                    rm_tm_mark_original_files(self, directory);
                }
            }

            if(self->session->cfg->write_unfinished) {
                rm_tm_write_unfinished_cksums(self, directory);
            }
        }

        if(result_dirs.length >= 2) {
            rm_shred_forward_to_output(self->session, &file_adaptor_group);
        }

        g_queue_foreach(&file_adaptor_group, (GFunc)g_free, NULL);
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
        bool has_one_dupe = false;
        RmOff file_size_acc = 0;

        GList *next = NULL;
        for(GList *iter = file_list->head; iter; iter = next) {
            RmFile *file = iter->data;
            next = iter->next;

            bool is_duplicate = g_hash_table_contains(self->file_checks, file->digest);
            has_one_dupe |= is_duplicate;

            /* with --partial-hidden we do not want to output */
            if(self->session->cfg->partial_hidden && file->is_hidden) {
                g_queue_delete_link(file_list, iter);
                continue;
            }

            if(iter != file_list->head && !is_duplicate) {
                file_size_acc += file->file_size;
            }
        }

        if(file_list->length >= 2) {
            /* If no separate duplicate files are requested, we can stop here */
            if(self->session->cfg->find_duplicates == false) {
                self->session->total_lint_size -= file_size_acc;
                self->session->dup_group_counter -= 1;
                self->session->dup_counter -= file_list->length - 1;
            } else {
                rm_shred_group_find_original(self->session, file_list);
                rm_shred_forward_to_output(self->session, file_list);
            }
        }
    }
}

static void rm_tm_cluster_up(RmTreeMerger *self, RmDirectory *directory) {
    char *parent_dir = g_path_get_dirname(directory->dirname);
    bool is_root = strcmp(parent_dir, "/") == 0;

    /* Lookup if we already found this parent before (if yes, merge with it) */
    RmDirectory *parent =
        rm_trie_search(&self->dir_tree, parent_dir);

    if(parent == NULL) {
        /* none yet, basically copy child */
        parent = rm_directory_new(parent_dir);
        rm_trie_insert(&self->dir_tree, parent_dir, parent);

        /* Get the actual file count */
        parent->file_count = GPOINTER_TO_UINT(
            rm_trie_search(&self->count_tree, parent_dir));

    } else {
        g_free(parent_dir);
    }

    rm_directory_add_subdir(parent, directory);

    if(parent->dupe_count == parent->file_count && parent->file_count > 0) {
        rm_tm_insert_dir(self, parent);
        if(!is_root) {
            rm_tm_cluster_up(self, parent);
        }
    }
}

void rm_tm_finish(RmTreeMerger *self) {
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

    if(!rm_session_was_aborted(self->session)) {
        /* Recursively call self to march on */
        rm_tm_extract(self);
    }
}
