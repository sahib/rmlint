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

#ifndef RM_PATHTRICIA_H
#define RM_PATHTRICIA_H

#include <glib.h>
#include <stdbool.h>

typedef struct _RmNode {
    /* Element of the path */
    char *basename;

    /* Parent node or NULL */
    struct _RmNode *parent;

    /* Array of children nodes */
    GHashTable *children;

    /* data was set explicitly */
    char has_value : 1;

    /* User specific data */
    gpointer data;
} RmNode;

typedef struct _RmTrie {
    /* Root node or NULL if empty */
    RmNode *root;

    /* chunk storage for strings */
    GStringChunk *chunks;

    /* size of the trie */
    size_t size;

    /* read write lock for insert/search */
    GMutex lock;
} RmTrie;

/* Callback to rm_trie_iter */
typedef int (*RmTrieIterCallback)(RmTrie *self, RmNode *node, int level, void *user_data);

/**
 * rm_trie_init:
 * Initialize a trie.
 */
void rm_trie_init(RmTrie *self);

/**
 * rm_trie_destroy:
 * Free all resources associated with the trie
 */
void rm_trie_destroy(RmTrie *self);

/**
 * rm_trie_insert:
 * Insert a path to the trie and associate a value with it.
 * The value can be later requested with rm_trie_search*.
 */
RmNode *rm_trie_insert(RmTrie *self, const char *path, void *value);
RmNode *rm_trie_insert_unlocked(RmTrie *self, const char *path, void *value);

/**
 * rm_trie_search_node:
 * Search a node in the trie by path.
 */
RmNode *rm_trie_search_node(RmTrie *self, const char *path);

/**
 * rm_trie_search:
 * As rm_trie_search_node but returns node->value if node is not NULL.
 */
void *rm_trie_search(RmTrie *self, const char *path);

/**
 * rm_trie_set_value:
 * Search a node in the trie and set its value.
 * If node does not exist, no value is set and false is returned..
 */
bool rm_trie_set_value(RmTrie *self, const char *path, void *data);

/**
 * rm_trie_build_path:
 * Take a node and go up till parent while writing all nodes
 * in buf (or until buf_len is reached).
 *
 * Returns the input buffer for chaining calls.
 */
char *rm_trie_build_path(RmTrie *self, RmNode *node, char *buf, size_t buf_len);
char *rm_trie_build_path_unlocked(RmNode *node, char *buf, size_t buf_len);

/**
 * rm_trie_size:
 * Return the size of the trie.
 */
size_t rm_trie_size(RmTrie *self);

/**
 * rm_trie_iter:
 * Iterate over all nodes in the trie starting on root calling `callback` on
 * each. If root is NULL, self->root is assumed.
 *
 * If pre_order is true, the trie is iterated top-down (i.e. for printing)
 * if pre_order is false, the trie is iterated bottom-up (i.e. for freeing)
 *
 * If all_nodes is true all nodes are traversed.
 * If all_nodes is false only nodes that were explicitly inserted are traversed.
 *
 * user_data will be passed to the callback.
 */
void rm_trie_iter(RmTrie *self,
                  RmNode *root,
                  bool pre_order,
                  bool all_nodes,
                  RmTrieIterCallback callback,
                  void *user_data);

#endif
