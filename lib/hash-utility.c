/**
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
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2020 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2020 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/config.h"
#include "../lib/hasher.h"
#include "../lib/utilities.h"

typedef struct RmHasherSession {
    /* Internal */
    char **paths;
    gint path_index;
    GMutex lock;
    RmDigest **completed_digests_buffer;
    bool *read_succesful;

    /* Options */
    RmDigestType digest_type;
    gboolean print_in_order;
} RmHasherSession;

static gboolean rm_hasher_parse_type(_UNUSED const char *option_name,
                                     const gchar *value,
                                     RmHasherSession *session,
                                     GError **error) {
    session->digest_type = rm_string_to_digest_type(value);

    if(session->digest_type == RM_DIGEST_UNKNOWN) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Unknown hash algorithm: '%s'"), value);
        return FALSE;
    }
    return TRUE;
}

static void rm_hasher_print(RmDigest *digest, char *path) {
    gsize size = rm_digest_get_bytes(digest) * 2 + 1;

    char checksum_str[size];
    memset(checksum_str, '0', size);
    checksum_str[size - 1] = 0;

    rm_digest_hexstring(digest, checksum_str);

    g_print("%s  %s\n", checksum_str, path);
}

static int rm_hasher_callback(_UNUSED RmHasher *hasher,
                              RmDigest *digest,
                              RmHasherSession *session,
                              gpointer index_ptr) {
    gint index = GPOINTER_TO_INT(index_ptr);

    g_mutex_lock(&session->lock);
    {
        if(session->print_in_order && digest) {
            /* add digest in buffer array */
            session->completed_digests_buffer[index] = digest;

            /* check if the next due digest has been completed; if yes then print
             * it (and possibly any following digests) */
            while(session->completed_digests_buffer[session->path_index]) {
                if(session->paths[session->path_index]) {
                    if(session->read_succesful[session->path_index]) {
                        rm_hasher_print(
                            session->completed_digests_buffer[session->path_index],
                            session->paths[session->path_index]);
                    }
                    rm_digest_free(
                        session->completed_digests_buffer[session->path_index]);
                }
                session->completed_digests_buffer[session->path_index] = NULL;
                session->path_index++;
            }
        } else if(digest) {
            if(session->read_succesful[session->path_index]) {
                rm_hasher_print(digest, session->paths[index]);
            }
        }
    }
    g_mutex_unlock(&session->lock);
    return 0;
}

int rm_hasher_main(int argc, const char **argv) {
    RmHasherSession tag;

    /* List of paths we got passed (or NULL)   */
    tag.paths = NULL;

    /* Print hashes in the same order as files in command line args */
    tag.print_in_order = TRUE;

    /* Digest type */
    tag.digest_type = RM_DEFAULT_DIGEST;
    gint threads = 8;
    gint64 buffer_mbytes = 256;
    guint64 increment = 4096;

    ////////////// Option Parsing ///////////////

    /* clang-format off */

    const GOptionEntry entries[] = {
        {"algorithm"      , 'a'  , 0                      , G_OPTION_ARG_CALLBACK        , (GOptionArgFunc)rm_hasher_parse_type  , _("Digest type [BLAKE2B]")                                                        , "[TYPE]"}   ,
        {"num-threads"    , 't'  , 0                      , G_OPTION_ARG_INT             , &threads                              , _("Number of hashing threads [8]")                                                 , "N"}        ,
        {"buffer-mbytes"  , 'b'  , 0                      , G_OPTION_ARG_INT64           , &buffer_mbytes                        , _("Megabytes read buffer [256 MB]")                                                , "MB"}       ,
        {"increment"      , 'x'  , G_OPTION_FLAG_HIDDEN   , G_OPTION_ARG_INT64           , &increment                            , _("bytes to hash at a time [4096]")                                                , "MB"}       ,
        {"ignore-order"   , 'i'  , G_OPTION_FLAG_REVERSE  , G_OPTION_ARG_NONE            , &tag.print_in_order                   , _("Print hashes in order completed, not in order entered (reduces memory usage)")  , NULL}       ,
        {""               , 0    , 0                      , G_OPTION_ARG_FILENAME_ARRAY  , &tag.paths                            , _("Space-separated list of files")                                                 , "[FILE…]"}  ,
        {NULL             , 0    , 0                      , 0                            , NULL                                  , NULL                                                                               , NULL}};

    /* clang-format on */

    GError *error = NULL;
    GOptionContext *context = g_option_context_new(_("Hash a list of files"));
    GOptionGroup *main_group =
        g_option_group_new(argv[0], _("Hash a list of files"), "", &tag, NULL);

    char summary[4096];
    memset(summary, 0, sizeof(summary));

    g_snprintf(summary, sizeof(summary),
               _("Multi-threaded file digest (hash) calculator.\n"
                 "\n  Available digest types:"
                 "\n  Cryptographic:"
                 "\n    %s\n"
                 "\n  Non-cryptographic:"
                 "\n    %s\n"
                 "\n  Supported, but not useful:"
                 "\n    %s\n"),
               "sha{1,256,512}, sha3-{256,384,512}, blake{2s,2b,2sp,2bp}, highway{64,128,256}",
#if HAVE_MM_CRC32_U64
               "metrocrc, metrocrc256, "
#endif
               "metro, metro256, xxhash, murmur",
               "cumulative, paranoid, ext");

    g_option_group_add_entries(main_group, entries);
    g_option_context_set_main_group(context, main_group);
    g_option_context_set_summary(context, summary);

    if(!g_option_context_parse(context, &argc, (char ***)&argv, &error)) {
        /* print g_option error message */
        rm_log_error_line("%s", error->message);
        exit(EXIT_FAILURE);
    }

    if(tag.paths == NULL) {
        /* read paths from stdin */
        char path_buf[PATH_MAX];
        char *tokbuf = NULL;
        GPtrArray *paths = g_ptr_array_new();

        while(fgets(path_buf, PATH_MAX, stdin)) {
            char *abs_path = realpath(strtok_r(path_buf, "\n", &tokbuf), NULL);
            g_ptr_array_add(paths, abs_path);
        }

        tag.paths = (char **)g_ptr_array_free(paths, FALSE);
    }

    if(tag.paths == NULL || tag.paths[0] == NULL) {
        rm_log_error_line(_("No valid paths given"));
        exit(EXIT_FAILURE);
    }

    g_option_context_free(context);

    ////////// Implementation //////

#if HAVE_MM_CRC32_U64 && HAVE_BUILTIN_CPU_SUPPORTS
    rm_digest_enable_sse(TRUE);
#endif

    int buf_size = (g_strv_length(tag.paths) + 1) * sizeof(RmDigest *);
    tag.read_succesful = g_slice_alloc0(buf_size);

    if(tag.print_in_order) {
        /* allocate buffer to collect results */
        tag.completed_digests_buffer = g_slice_alloc0(buf_size);
        tag.path_index = 0;
    }

    /* initialise structures */
    g_mutex_init(&tag.lock);
    RmHasher *hasher = rm_hasher_new(tag.digest_type,
                                     threads,
                                     FALSE,
                                     increment,
                                     1024 * 1024 * buffer_mbytes,
                                     (RmHasherCallback)rm_hasher_callback,
                                     &tag);

    /* Iterate over paths, pushing to hasher threads */
    for(int i = 0; tag.paths && tag.paths[i]; ++i) {
        /* check it is a regular file */

        RmStat stat_buf;
        if(rm_sys_stat(tag.paths[i], &stat_buf) == -1) {
            rm_log_warning_line(_("Can't open directory or file \"%s\": %s"),
                                tag.paths[i], strerror(errno));
        } else if(S_ISDIR(stat_buf.st_mode)) {
            rm_log_warning_line(_("Directories are not supported: %s"), tag.paths[i]);
        } else if(S_ISREG(stat_buf.st_mode)) {
            RmHasherTask *task = rm_hasher_task_new(hasher, NULL, GINT_TO_POINTER(i));
            tag.read_succesful[i] =
                rm_hasher_task_hash(task, tag.paths[i], 0, stat_buf.st_size, FALSE, NULL);

            rm_hasher_task_finish(task);
            continue;
        } else {
            rm_log_warning_line(_("%s: Unknown file type"), tag.paths[i]);
        }

        /* dummy callback for failed paths */
        g_free(tag.paths[i]);
        tag.paths[i] = NULL;
        rm_hasher_callback(hasher, NULL, &tag, GINT_TO_POINTER(i));
    }

    /* wait for all hasher threads to finish... */
    rm_hasher_free(hasher, TRUE);

    /* tidy up */
    g_slice_free1(buf_size, tag.read_succesful);
    if(tag.print_in_order) {
        g_slice_free1(buf_size, tag.completed_digests_buffer);
    }

    g_strfreev(tag.paths);

    return EXIT_SUCCESS;
}
