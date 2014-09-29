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

static bool du_files(char **files, int bit_flags) {
    if (*files == NULL) {
        return false;
    }

    FTS *fts = fts_open(files, bit_flags, NULL);

    while(1) {
        FTSENT *ent = fts_read (fts);

        g_printerr("%d: %s %s\n", ent->fts_level, (ent->fts_info == FTS_DP) ? "post" : "pre", ent->fts_path);
    }

    if (fts_close (fts) != 0) {
        rm_log_perror("fts_close failed");
        return false;
    }

    return true;
}

// static RmTreeMerger * rm_tm_new(char **dir_vec) {
//     
// }

int main(void) {
    char *dirs[2] = {"/tmp", NULL};
    du_files(dirs, 0);
}
