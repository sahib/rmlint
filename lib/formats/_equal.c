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

#include "../formats.h"
#include "../utilities.h"
#include "../preprocess.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

typedef struct RmFmtHandlerEqual {
    /* must be first */
    RmFmtHandler parent;

	/* Checksum of the last checked file (or NULL) */
	char *last_checksum;

	/* Set to true once a mismatch (colliding cksum) was found */
	bool mismatch_found;
} RmFmtHandlerEqual;

/////////////////////////
//  ACTUAL CALLBACKS   //
/////////////////////////

static void rm_fmt_elem(
    RmSession *session,
    RmFmtHandler *self_ref,
    _UNUSED FILE *out,
	RmFile *file
) {
    RmFmtHandlerEqual *self = (RmFmtHandlerEqual *)self_ref;

	/*  No need to check anymore, it's not equal. */
	if(self->mismatch_found) {
		return;
	}

	if(file->digest == NULL) {
		/* We do not want to handle unique files here.
		 * If it is unique, it will be not equal...
		 * */
		self->mismatch_found = true;
		return;
	}

	RM_DEFINE_PATH(file);

	bool path_explictly_given = false;
	GSList *paths = session->cfg->paths;
	for(GSList *iter = paths; iter; iter = iter->next) {
		RmPath *path = iter->data;
		if(!strcmp(path->path, file_path)) {
			path_explictly_given = true;
		}
	}

	if(path_explictly_given == false) {
		/* Ignore, this path was not given explicitly. */
		return;
	}

	size_t cksum_bytes = rm_digest_get_bytes(file->digest) * 2 + 1;
	char *checksum = g_malloc0(cksum_bytes);

    memset(checksum, '0', cksum_bytes);
    checksum[cksum_bytes - 1] = 0;
    rm_digest_hexstring(file->digest, checksum);

	if(self->last_checksum != NULL) {
		if(!strncmp(checksum, self->last_checksum, cksum_bytes)) {
			session->equal_exit_code = EXIT_SUCCESS;
		} else {
			session->equal_exit_code = EXIT_FAILURE;
			self->mismatch_found = true;
			rm_log_debug_line(
					"First differing items:\n\t%s (%s)\n\tlast checksum: (%s)",
					file_path, checksum, self->last_checksum
			);
		}
		g_free(self->last_checksum);
	}

	self->last_checksum = checksum;
}

static void rm_fmt_foot(
		_UNUSED RmSession *session,
		RmFmtHandler *parent,
		_UNUSED FILE *out) {
    RmFmtHandlerEqual *self = (RmFmtHandlerEqual *)parent;
	g_free(self->last_checksum);
}

static RmFmtHandlerEqual EQUAL_HANDLE_IMPL = {
    /* Initialize parent */
    .parent = {
        .size = sizeof(EQUAL_HANDLE_IMPL),
        .name = "_equal",
        .head = NULL,
        .elem = rm_fmt_elem,
        .prog = NULL,
        .foot = rm_fmt_foot,
        .valid_keys = {NULL},
    },
	.last_checksum = NULL,
	.mismatch_found = false
};

RmFmtHandler *EQUAL_HANDLER = (RmFmtHandler *) &EQUAL_HANDLE_IMPL;
