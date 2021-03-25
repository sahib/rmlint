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
#include <stdio.h>

#include "logger.h"
#include "utilities.h"


static void print_usage(GOptionContext *context) {
    char* usage = g_option_context_get_help(context, TRUE, NULL);
    printf("%s", usage);
    g_free(usage);
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
