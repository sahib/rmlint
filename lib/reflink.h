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

#ifndef RM_REFLINK_H
#define RM_REFLINK_H

#include "file.h"

/**
 * @file reflink.h
 * @brief Launchers for reflink-related utilities
 **/

/**
 * @brief
 *
 * @param argc arg count passed from main
 * @param argv command line args; argv[0] is normally "rmlint-dedupe"
 * @retval EXIT_SUCCESS or EXIT_FAILURE
 *
 **/
int rm_dedupe_main(int argc, const char **argv);

/**
 * @brief
 * the linux OS doesn't provide any easy way to check if two files are
 * reflinks / clones (eg:
 * https://unix.stackexchange.com/questions/263309/how-to-verify-a-file-copy-is-reflink-cow
 *
 * `rmlint --is-reflink file_a file_b` provides this functionality rmlint.
 * return values:
 *
 *
 * @param argc arg count passed from main
 * @param argv command line args; argv[0] is normally "rmlint-is-reflink"
 * @retval EXIT_SUCCESS if clone confirmed, EXIT_FAILURE if definitely not clones,
 * Other return values defined in utilities.h 'RmOffsetsMatchCode' enum
 *
 **/
int rm_is_reflink_main(int argc, const char **argv);

RmLinkType rm_reflink_type_from_fd(int fd1, int fd2);

#endif /* end of include guard */
