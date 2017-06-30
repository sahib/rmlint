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
 *  - Christopher <sahib> Pahl 2010-2017 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2017 (https://github.com/SeeSpotRun)
 *
 * Hosted on http://github.com/sahib/rmlint
 *
 */

#ifndef RM_COUNTERS_H
#define RM_COUNTERS_H

#include <glib.h>
#include <stdbool.h>

typedef gint64 RmCounter;
#define RM_COUNTER_FORMAT "li"

typedef enum RmCounterID {
    RM_COUNTER_TOTAL_FILES = 0,
    RM_COUNTER_IGNORED_FILES,
    RM_COUNTER_IGNORED_FOLDERS,

    RM_COUNTER_TOTAL_FILTERED_FILES,
    RM_COUNTER_TOTAL_LINT_SIZE,
    RM_COUNTER_SHRED_BYTES_REMAINING,

    RM_COUNTER_SHRED_BYTES_TOTAL,
    RM_COUNTER_SHRED_FILES_REMAINING,
    RM_COUNTER_SHRED_BYTES_AFTER_PREPROCESS,
    RM_COUNTER_DUP_COUNTER,
    RM_COUNTER_DUP_GROUP_COUNTER,
    RM_COUNTER_OTHER_LINT_CNT,

    RM_COUNTER_DUPLICATE_BYTES,
    RM_COUNTER_UNIQUE_BYTES,
    RM_COUNTER_ORIGINAL_BYTES,
    RM_COUNTER_SHRED_BYTES_READ,

    /* DEBUGGING COUNTERS */
    RM_COUNTER_OFFSET_FRAGMENTS,
    RM_COUNTER_OFFSETS_READ,
    RM_COUNTER_OFFSET_FAILS,

    RM_COUNTER_LAST,
} RmCounterID;

/**
 * @brief initialise session counters
 **/
void rm_counter_session_init(void);

/**
 * @brief free resources allocated by rm_counter_session_init()
 **/
void rm_counter_session_free(void);

/**
 * @brief return elapsed time in seconds since rm_counter_session_init()
 **/
gdouble rm_counter_elapsed_time(void);

/**
 * @brief add increment to the specified counter and return the new value
 * @param counter the counter ID
 * @param increment the amount to add to counter
 **/
RmCounter rm_counter_add_and_get(RmCounterID counter, RmCounter increment);

/**
 * @brief same as rm_counter_add_and_get but not threadsafe
 **/
RmCounter rm_counter_add_and_get_unlocked(RmCounterID counter, RmCounter increment);

/**
 * @brief set the specified counter's value
 * @param counter the counter ID
 * @param value the new value
 **/
void rm_counter_set(RmCounterID counter, RmCounter value);

/**
 * @brief same as rm_counter_set but not threadsafe
 **/
void rm_counter_set_unlocked(RmCounterID counter, RmCounter value);


/** convenience macros **/
#define rm_counter_add(counter, increment) \
    ((void)rm_counter_add_and_get(counter, increment))

#define rm_counter_add_unlocked(counter, increment) \
    ((void)rm_counter_add_and_get_unlocked(counter, increment))

#define rm_counter_get(counter) (rm_counter_add_and_get(counter, 0))

#define rm_counter_get_unlocked(counter) (rm_counter_add_and_get_unlocked(counter, 0))

#endif /* end of include guard */
