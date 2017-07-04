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

#include "counters.h"
typedef struct RmCounterStruct {
    RmCounter value;
    GMutex lock;
} RmCounterStruct;

/**
 * a global set of counters
 **/
static RmCounterStruct rm_counters[RM_COUNTER_LAST];

/**
 * global session timer
 **/
GTimer *rm_counter_session_timer = NULL;


void rm_counter_session_init(void) {
    rm_counter_session_timer = g_timer_new();

    for(gint i = 0; i < RM_COUNTER_LAST; i++) {
        rm_counters[i].value = 0;
    }
}

void rm_counter_session_free(void) {
    for(gint i = 0; i < RM_COUNTER_LAST; i++) {
        rm_counters[i].value = 0;
    }
    g_timer_destroy(rm_counter_session_timer);
}

gdouble rm_counter_elapsed_time(void) {
    return g_timer_elapsed(rm_counter_session_timer, NULL);
}

RmCounter rm_counter_add_and_get_unlocked(RmCounterID counter, RmCounter increment) {
    return (rm_counters[counter].value += increment);
}

RmCounter rm_counter_add_and_get(RmCounterID counter, RmCounter increment) {
    gint64 value;
    g_mutex_lock(&rm_counters[counter].lock);
    { value = rm_counter_add_and_get_unlocked(counter, increment); }
    g_mutex_unlock(&rm_counters[counter].lock);
    return value;
}

void rm_counter_set_unlocked(RmCounterID counter, RmCounter value) {
    rm_counters[counter].value = value;
}

void rm_counter_set(RmCounterID counter, RmCounter value) {
    g_mutex_lock(&rm_counters[counter].lock);
    { rm_counter_set_unlocked(counter, value); }
    g_mutex_unlock(&rm_counters[counter].lock);
}
