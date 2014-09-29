#include <glib.h>
#include <string.h>
#include <fts.h>

#include "file.h"
#include "session.h"
#include "libart/art.h"

typedef struct RmTreeMerger {
    RmSession *session;        /* Session state variables / Settings       */
    art_tree file_tree;        /* Path-Trie with all RmFiles as value      */
    art_tree du_tree;          /* Path-Trie with all file's count as value */
    GHashTable *result_table;  /* Mapping of hash-str => [RmDirectory]     */
    GQueue valid_dirs;         /* Directories consisting of RmFiles only   */
} RmTreeMerger;

typedef struct RmDirectory {
    char *directory;           /* Path to this directory without trailing slash   */
    RmDigest common_digest;    /* Hash of hashes of known_files in this directory */
    GQueue *known_files;       /* RmFiles in this directory                       */
    RmOff file_count;          /* Count of files actually in this directory       */
} RmDirectory;

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
     */
    for(int i = key_len; i >= 0; --i) {
        if(path[i] == G_DIR_SEPARATOR) {
            /* Do not use an empty path, use a / */
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

static int xx(_U void * data, const unsigned char * key, _U uint32_t key_len, _U void * value) {
    g_printerr("%u: %s\n", GPOINTER_TO_UINT(value), key);
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
    art_iter(&dir_tree, xx, NULL);
    destroy_art_tree(&dir_tree);
}

#endif
