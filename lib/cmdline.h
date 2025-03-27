/**
* This file is part of rmlint.
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
**/

#ifndef RM_CMDLINE_H
#define RM_CMDLINE_H

#include "session.h"

/**
 * @brief Parse all arguments specified in argc/argv and set the aprop. cfg
 * in session->cfg.
 *
 * @return false on parse error (running makes no sense then)
 */
bool rm_cmd_parse_args(int argc, char **argv, RmSession *session);

/**
 * @brief Trigger the main method of rmlint.
 *
 * @return exit_status for exit()
 */
int rm_cmd_main(RmSession *session);

#if RM_IS_APPLE
#include <AvailabilityMacros.h>
#if (MAC_OS_X_VERSION_MAX_ALLOWED < 1070)
/* there is no getdelim */
#if __STDC_VERSION__ >= 199901L
/* restrict is a keyword */
#else
# define restrict
#endif

#ifndef _POSIX_SOURCE
typedef long ssize_t;
#define SSIZE_MAX LONG_MAX
#endif

ssize_t __attribute__((weak)) getdelim(char **restrict lineptr, size_t *restrict n, int delimiter,
                 FILE *restrict stream);
ssize_t __attribute__((weak)) getline(char **restrict lineptr, size_t *restrict n,
                FILE *restrict stream);

#define _GETDELIM_GROWBY 128    /* amount to grow line buffer by */
#define _GETDELIM_MINLEN 4      /* minimum line buffer size */

ssize_t getdelim(char **restrict lineptr, size_t *restrict n, int delimiter,
                 FILE *restrict stream)
{
	char *buf, *pos;
	int c;
	ssize_t bytes;

	if (lineptr == NULL || n == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (stream == NULL) {
		errno = EBADF;
		return -1;
	}

	/* resize (or allocate) the line buffer if necessary */
	buf = *lineptr;
	if (buf == NULL || *n < _GETDELIM_MINLEN) {
		buf = realloc(*lineptr, _GETDELIM_GROWBY);
		if (buf == NULL) {
			/* ENOMEM */
			return -1;
		}
		*n = _GETDELIM_GROWBY;
		*lineptr = buf;
	}

	/* read characters until delimiter is found, end of file is reached, or an error occurs. */
	bytes = 0;
	pos = buf;
	while ((c = getc(stream)) != EOF) {
		if (bytes + 1 >= SSIZE_MAX) {
			errno = EOVERFLOW;
			return -1;
		}
		bytes++;
		if (bytes >= *n - 1) {
			buf = realloc(*lineptr, *n + _GETDELIM_GROWBY);
			if (buf == NULL) {
				/* ENOMEM */
				return -1;
			}
			*n += _GETDELIM_GROWBY;
			pos = buf + bytes - 1;
			*lineptr = buf;
		}

		*pos++ = (char) c;
		if (c == delimiter) {
			break;
		}
	}

	if (ferror(stream) || (feof(stream) && (bytes == 0))) {
		/* EOF, or an error from getc(). */
		return -1;
	}

	*pos = '\0';
	return bytes;
}

ssize_t getline(char **restrict lineptr, size_t *restrict n,
                FILE *restrict stream)
{
	return getdelim(lineptr, n, '\n', stream);
}
#endif
#endif /* RM_IS_APPLE */

#endif /* RM_CMDLINE_H */
