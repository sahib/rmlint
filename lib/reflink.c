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

#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "config.h"

#if HAVE_BTRFS_H
#include <linux/btrfs.h>
#endif

#if HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif

#ifdef FIDEDUPERANGE
#define HAVE_FIDEDUPERANGE 1
#else
#define HAVE_FIDEDUPERANGE 0
#endif

#include "logger.h"
#include "utilities.h"
#include "xattr.h"

/* FIDEDUPERANGE supercedes the btrfs-only BTRFS_IOC_FILE_EXTENT_SAME as of Linux 4.5 and
 * should work for ocfs2 and xfs as well as btrfs.  We should still support the older
 * btrfs ioctl so that this still works on Linux 4.2 to 4.4.  The two ioctl's are
 * identical apart from field names so we can use #define's to accommodate both. */

/* TODO: test this on system running kernel 4.[2|3|4] ; if the c headers
 * support FIDEDUPERANGE but kernel doesn't, then this will fail at runtime
 * because the BTRFS_IOC_FILE_EXTENT_SAME is decided at compile time...
 */

#if HAVE_FIDEDUPERANGE
# define _DEDUPE_IOCTL_NAME        "FIDEDUPERANGE"
# define _DEDUPE_IOCTL             FIDEDUPERANGE
# define _DEST_FD                  dest_fd
# define _SRC_OFFSET               src_offset
# define _DEST_OFFSET              dest_offset
# define _SRC_LENGTH               src_length
# define _DATA_DIFFERS             FILE_DEDUPE_RANGE_DIFFERS
# define _FILE_DEDUPE_RANGE        file_dedupe_range
# define _FILE_DEDUPE_RANGE_INFO   file_dedupe_range_info
# define _MIN_LINUX_SUBVERSION     5
#else
# define _DEDUPE_IOCTL_NAME        "BTRFS_IOC_FILE_EXTENT_SAME"
# define _DEDUPE_IOCTL             BTRFS_IOC_FILE_EXTENT_SAME
# define _DEST_FD                  fd
# define _SRC_OFFSET               logical_offset
# define _DEST_OFFSET              logical_offset
# define _SRC_LENGTH               length
# define _DATA_DIFFERS             BTRFS_SAME_DATA_DIFFERS
# define _FILE_DEDUPE_RANGE        btrfs_ioctl_same_args
# define _FILE_DEDUPE_RANGE_INFO   btrfs_ioctl_same_extent_info
# define _MIN_LINUX_SUBVERSION     2
#endif


RmLinkType rm_reflink_type_from_fd(int fd1, int fd2, guint64 file_size) { 
#if HAVE_FIEMAP

    RmOff logical_current = 0;

    bool is_last_1 = false;
    bool is_last_2 = false;
    bool is_inline_1 = false;
    bool is_inline_2 = false;
    bool at_least_one_checked = false;

    while(!rm_session_was_aborted()) {
        RmOff logical_next_1 = 0;
        RmOff logical_next_2 = 0;

        RmOff physical_1 = rm_offset_get_from_fd(fd1, logical_current, &logical_next_1, &is_last_1, &is_inline_1);
        RmOff physical_2 = rm_offset_get_from_fd(fd2, logical_current, &logical_next_2, &is_last_2, &is_inline_2);

        if(is_last_1 != is_last_2) {
            return RM_LINK_NONE;
        }

        if(is_last_1 && is_last_2 && at_least_one_checked) {
            return RM_LINK_REFLINK;
        }

        if(physical_1 != physical_2) {
#if _RM_OFFSET_DEBUG
            rm_log_debug_line("Physical offsets differ at byte %" G_GUINT64_FORMAT
                              ": %"G_GUINT64_FORMAT "<> %" G_GUINT64_FORMAT,
                              logical_current, physical_1, physical_2);
#endif
            return RM_LINK_NONE;
        }
        if(logical_next_1 != logical_next_2) {
#if _RM_OFFSET_DEBUG
            rm_log_debug_line("File offsets differ after %" G_GUINT64_FORMAT
                              " bytes: %" G_GUINT64_FORMAT "<> %" G_GUINT64_FORMAT,
                              logical_current, logical_next_1, logical_next_2);
#endif
            return RM_LINK_NONE;
        }

        if(is_inline_1 || is_inline_2) {
            return RM_LINK_INLINE_EXTENTS;
        }

        if(physical_1 == 0) {
#if _RM_OFFSET_DEBUG
            rm_log_debug_line(
                "Can't determine whether files are clones");
#endif
            return RM_LINK_ERROR;
        }

#if _RM_OFFSET_DEBUG
        rm_log_debug_line("Offsets match at fd1=%d, fd2=%d, logical=%" G_GUINT64_FORMAT ", physical=%" G_GUINT64_FORMAT,
                          fd1, fd2, logical_current, physical_1);
#endif
        if(logical_next_1 <= logical_current) {
            /* oops we seem to be getting nowhere (this shouldn't really happen) */
            rm_log_info_line(
                "rm_util_link_type() giving up: file1_offset_next<=file_offset_current");
            return RM_LINK_ERROR;
        }

        if(logical_next_1 >= (guint64)file_size) {
            /* phew, we got to the end */
#if _RM_OFFSET_DEBUG
            rm_log_debug_line("Files are clones (share same data)")
#endif
            return RM_LINK_REFLINK;
        }

        logical_current = logical_next_1;
        at_least_one_checked = true;
    }

    return RM_LINK_ERROR;
#else
    return RM_LINK_NONE;
#endif
}

static void print_usage(GOptionContext *context) {
    char* usage = g_option_context_get_help(context, TRUE, NULL);
    printf("%s", usage);
    g_free(usage);
}


/**
 * *********** dedupe session main ************
 **/
int rm_dedupe_main(int argc, const char **argv) {


    gboolean check_xattr = FALSE;
    gboolean dedupe_readonly = FALSE;
    gboolean follow_symlinks = FALSE;
    gboolean skip_inline_extents = TRUE;

    const GOptionEntry options[] = {
        {"xattr"         , 'x' , 0                     , G_OPTION_ARG_NONE     , &check_xattr       , _("Check extended attributes to see if the file is already deduplicated") , NULL},
        {"readonly"      , 'r' , 0                     , G_OPTION_ARG_NONE     , &dedupe_readonly   , _("Even dedupe read-only snapshots (needs root)")                         , NULL},
        {"followlinks"   , 'f' , 0                     , G_OPTION_ARG_NONE     , &follow_symlinks   , _("Follow symlinks")                                                      , NULL},
        {"inline-extents", 'i' , G_OPTION_FLAG_REVERSE , G_OPTION_ARG_NONE     , &follow_symlinks   , _("Try to dedupe files with inline extents")                              , NULL},
        {"loud"          , 'v' , G_OPTION_FLAG_NO_ARG  , G_OPTION_ARG_CALLBACK , rm_logger_louder   , _("Be more verbose (-vvv for much more)")                                 , NULL},
        {"quiet"         , 'V' , G_OPTION_FLAG_NO_ARG  , G_OPTION_ARG_CALLBACK , rm_logger_quieter  , _("Be less verbose (-VVV for much less)")                                 , NULL},
        {NULL            , 0   , 0                     , 0                     , NULL               , NULL                                                                      , NULL}};


    GError *error = NULL;
    GOptionContext *context = g_option_context_new ("rmlint --dedupe: deduplicate two identical files on a reflink-capable filesystem");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_set_help_enabled(context, TRUE);
    if (!g_option_context_parse (context, &argc, (char ***)&argv, &error))
    {
        rm_log_error_line("Error parsing command line:\n%s", error->message);
        return(EXIT_FAILURE);
    }

    g_option_context_free(context);

#if HAVE_FIDEDUPERANGE || HAVE_BTRFS_H
    if(argc != 3) {
        rm_log_error(_("rmlint --dedupe must have exactly two files\n"));
        return EXIT_FAILURE;
    }

    const char* source_path = argv[1];
    const char* dest_path = argv[2];
    rm_log_debug_line("Cloning %s -> %s", source_path, dest_path);

    if(check_xattr) {
        // Check if we actually need to deduplicate.
        // This utility will write a value to the extended attributes
        // of the file so we know that we do not need to do it again
        // next time. This is supposed to avoid disk thrashing.
        // (See also: https://github.com/sahib/rmlint/issues/349)
        if(rm_xattr_is_deduplicated(dest_path, follow_symlinks)) {
            rm_log_debug_line("Already deduplicated according to xattr!");
            return EXIT_SUCCESS;
        }
    }

    // Also use --is-reflink on both files before doing extra work:

    RmLinkType link_type = rm_util_link_type(source_path, dest_path);
    if(link_type == RM_LINK_REFLINK) {
        rm_log_debug_line("Already an exact reflink!");
        return EXIT_SUCCESS;
    }
    else if (link_type == RM_LINK_INLINE_EXTENTS && skip_inline_extents) {
        rm_log_debug_line("Skipping files with inline extents");
        return EXIT_SUCCESS;
    }

    int source_fd = rm_sys_open(source_path, O_RDONLY);
    if(source_fd < 0) {
        rm_log_error_line(_("dedupe: failed to open source file"));
        return EXIT_FAILURE;
    }

    struct stat source_stat;
    fstat(source_fd, &source_stat);
    gint64 bytes_deduped = 0;

    /* a poorly-documented limit for dedupe ioctl's */
    static const gint64 max_dedupe_chunk = 16 * 1024 * 1024;

    /* how fine a resolution to use once difference detected;
     * use btrfs default node size (16k): */
    static const gint64 min_dedupe_chunk = 16 * 1024;

    rm_log_debug_line("Cloning using %s", _DEDUPE_IOCTL_NAME);

    if(!rm_session_check_kernel_version(4, _MIN_LINUX_SUBVERSION)) {
        rm_log_warning_line("This needs at least linux >= 4.%d.", _MIN_LINUX_SUBVERSION);
        return EXIT_FAILURE;
    }

    struct {
        struct _FILE_DEDUPE_RANGE args;
        struct _FILE_DEDUPE_RANGE_INFO info;
    } dedupe;
    memset(&dedupe, 0, sizeof(dedupe));

    dedupe.info._DEST_FD =
        rm_sys_open(dest_path, dedupe_readonly ? O_RDONLY : O_RDWR);

    if(dedupe.info._DEST_FD < 0) {
        rm_log_error_line(
            _("dedupe: error %i: failed to open dest file.%s"),
            errno,
            dedupe_readonly ? "" : _("\n\t(if target is a read-only snapshot "
                                          "then -r option is required)"));
        rm_sys_close(source_fd);
        return EXIT_FAILURE;
    }

    /* fsync's needed to flush extent mapping */
    if(fsync(source_fd) != 0) {
        rm_log_warning_line("Error syncing source file %s: %s", source_path,
                            strerror(errno));
    }

    if(fsync(dedupe.info._DEST_FD) != 0) {
        rm_log_warning_line("Error syncing dest file %s: %s", dest_path,
                            strerror(errno));
    }

    int ret = 0;
    gint64 dedupe_chunk = max_dedupe_chunk;
    while(bytes_deduped < source_stat.st_size && !rm_session_was_aborted()) {
        dedupe.args.dest_count = 1;
        /* TODO: multiple destinations at same time? */
        dedupe.args._SRC_OFFSET = bytes_deduped;
        dedupe.info._DEST_OFFSET = bytes_deduped;

        /* try to dedupe the rest of the file */
        dedupe.args._SRC_LENGTH = MIN(dedupe_chunk, source_stat.st_size - bytes_deduped);

        ret = ioctl(source_fd, _DEDUPE_IOCTL, &dedupe);

        if(ret != 0) {
            break;
        } else if(dedupe.info.status == _DATA_DIFFERS) {
            if(dedupe_chunk != min_dedupe_chunk) {
                dedupe_chunk = min_dedupe_chunk;
                rm_log_debug_line("Dropping to %"G_GINT64_FORMAT"-byte chunks "
                                  "after %"G_GINT64_FORMAT" bytes",
                                  dedupe_chunk, bytes_deduped);
                continue;
            } else {
                break;
            }
        } else if(dedupe.info.status != 0) {
            ret = -dedupe.info.status;
            errno = ret;
            break;
        } else if(dedupe.info.bytes_deduped == 0) {
            break;
        }

        bytes_deduped += dedupe.info.bytes_deduped;
    }
    rm_log_debug_line("Bytes deduped: %"G_GINT64_FORMAT, bytes_deduped);

    if (ret!=0) {
        rm_log_perrorf(_("%s returned error: (%d)"), _DEDUPE_IOCTL_NAME, ret);
    } else if(bytes_deduped == 0) {
        rm_log_info_line(_("Files don't match - not deduped"));
    } else if(bytes_deduped < source_stat.st_size) {
        rm_log_info_line(_("Only first %"G_GINT64_FORMAT" bytes deduped "
                           "- files not fully identical"),
                         bytes_deduped);
    }

    rm_sys_close(source_fd);
    rm_sys_close(dedupe.info._DEST_FD);

    if(bytes_deduped == source_stat.st_size) {
        if(check_xattr && !dedupe_readonly) {
            rm_xattr_mark_deduplicated(dest_path, follow_symlinks);
        }

        return EXIT_SUCCESS;
    }

#else
    (void)cfg;
    rm_log_error_line(_("rmlint was not compiled with file cloning support."))
#endif

    return EXIT_FAILURE;
}



/**
 * *********** `rmlint --is-reflink` session main ************
 **/
int rm_is_reflink_main(int argc, const char **argv) {

    const GOptionEntry options[] = {
        {"loud"          , 'v' , G_OPTION_FLAG_NO_ARG  , G_OPTION_ARG_CALLBACK , rm_logger_louder   , _("Be more verbose (-vvv for much more)")                                 , NULL},
        {"quiet"         , 'V' , G_OPTION_FLAG_NO_ARG  , G_OPTION_ARG_CALLBACK , rm_logger_quieter  , _("Be less verbose (-VVV for much less)")                                 , NULL},
        {NULL            , 0   , 0                     , 0                     , NULL               , NULL                                                                      , NULL}};


    GError *error = NULL;
    GOptionContext *context = g_option_context_new ("rmlint --is-reflink file1 files2: check if two files are reflinks (share data extents)");
    g_option_context_add_main_entries (context, options, NULL);
    g_option_context_set_help_enabled(context, TRUE);
    if (!g_option_context_parse (context, &argc, (char ***)&argv, &error))
    {
        rm_log_error_line("Error parsing command line:\n%s", error->message);
        return(EXIT_FAILURE);
    }

    g_option_context_free(context);

    if (argc != 3) {
        rm_log_error(_("rmlint --is-reflink must have exactly two arguments"));
        print_usage(context);
        return EXIT_FAILURE;
    }
    const char *a = argv[1];
    const char *b = argv[2];
    rm_log_debug_line("Testing if %s is clone of %s", a, b);

    int result = rm_util_link_type(a, b);
    switch(result) {
    case RM_LINK_REFLINK:
        rm_log_debug_line("Offsets match");
        break;
    case RM_LINK_NONE:
        rm_log_debug_line("Offsets differ");
        break;
    case RM_LINK_INLINE_EXTENTS:
        rm_log_debug_line("File[s] have inline extents so can't be reflinks");
        break;
    default:
        rm_log_debug_line("Can't determine if reflinks");
        break;
    }

    return result;
}
