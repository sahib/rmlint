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
*  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
*  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
*
* Hosted on http://github.com/sahib/rmlint
**/

#ifndef RM_SWAP_TABLE_H
#define RM_SWAP_TABLE_H

#include "config.h"

#include <glib.h>

#if HAVE_SQLITE3
# include <sqlite3.h>
#endif

/**
 * @brief A simple key-value store based on sqlite. 
 *
 * Currently, only storing arbitary data blobs is possible. 
 * There's no support for storing ints or structs since this
 * was not needed (but could be added via GVariant)
 *
 * The key is always an integer.
 */
typedef struct RmSwapTable {
#if HAVE_SQLITE3
    sqlite3 *cache;
#endif

    GPtrArray *attrs;

    /* Calls to insert/lookup are locked */
    GMutex mtx;

    /* True if a transaction was triggered */
    gboolean transaction_running;

    /* Variable path size, get's allocated on creation */
    char path[];
} RmSwapTable;

/**
 * @brief Create a new swapped table.
 *
 * @return the new table or NULL on error.
 */
RmSwapTable *rm_swap_table_open(gboolean in_memory, GError **error);

/**
 * @brief Close the table and delete all background resources.
 *
 * Possible errors are stored inside the error pointer.
 */
void rm_swap_table_close(RmSwapTable *self, GError **error);

/**
 * @brief Create a new attribute inside the table.
 *
 * Attributes can be thought as a "arena" of some certain values.
 * In background a table is created for each attribute.
 * Each attribute has their own id counter.
 *
 * @param name Name of the attribute. This has no practical use, but debugging.
 * @param error
 *
 * @return the id of the attribute for use with lookup/insert.
 */
int rm_swap_table_create_attr(RmSwapTable *self, const char *name, GError **error);

/**
 * @brief Insert a new value to the key-value table.
 *
 * You do not have to worry about transactions since a BEGIN IMMEDIATE; is
 * executed automatically on every insert if none was done yet.  On select a
 * COMMIT is done if a transaction is running.  This way, the caller has to
 * ensure only to do many inserts before doing a single select.
 *
 * @param attr attribute id.
 * @param data data blob to insert.
 * @param data_len Len of data.
 *
 * @return 0 on error, else a valid id.
 */
RmOff rm_swap_table_insert(RmSwapTable *self, int attr, char *data, size_t data_len);

/**
 * @brief Lookups a certain value in the table.
 *
 * @param attr attribute id.
 * @param id the id of the value as returned by insert.
 * @param buf buffer to write the result to.
 * @param buf_size size of buf.
 *
 * @return the number of bytes written to buf.
 */
size_t rm_swap_table_lookup(RmSwapTable *self, int attr, RmOff id, char *buf, size_t buf_size);

#endif /* end of include guard */

