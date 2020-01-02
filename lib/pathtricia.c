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

#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "pathtricia.h"

//////////////////////////
//  RmPathNode Methods  //
//////////////////////////

static RmNode *rm_node_new(RmTrie *trie, const char *elem) {
    RmNode *self = g_slice_alloc0(sizeof(RmNode));

    if(elem != NULL) {
        /* Note: We could use g_string_chunk_insert_const here.
         * That would keep a hash table with all strings internally though,
         * but would sort out storing duplicate path elements. In normal
         * setups this will not happen that much though I guess.
         */
        self->basename = g_string_chunk_insert(trie->chunks, elem);
    }
    return self;
}

static void rm_node_free(RmNode *node) {
    if(node->children) {
        g_hash_table_unref(node->children);
    }
    memset(node, 0, sizeof(RmNode));
    g_slice_free(RmNode, node);
}

static RmNode *rm_node_insert(RmTrie *trie, RmNode *parent, const char *elem) {
    if(parent->children == NULL) {
        parent->children = g_hash_table_new(g_str_hash, g_str_equal);
    }

    RmNode *exists = g_hash_table_lookup(parent->children, elem);
    if(exists == NULL) {
        RmNode *node = rm_node_new(trie, elem);
        node->parent = parent;
        g_hash_table_insert(parent->children, node->basename, node);
        return node;
    }

    return exists;
}

///////////////////////////
//    RmTrie Methods     //
///////////////////////////

void rm_trie_init(RmTrie *self) {
    g_assert(self);
    self->root = rm_node_new(self, NULL);

    /* Average path len is 93.633236.
     * I did ze science! :-)
     */
    self->chunks = g_string_chunk_new(100);

    g_mutex_init(&self->lock);
}

/* Path iterator that works with absolute paths.
 * Absolute paths are required to start with a /
 */
typedef struct RmPathIter {
    char *curr_elem;
    char path_buf[PATH_MAX];
} RmPathIter;

void rm_path_iter_init(RmPathIter *iter, const char *path) {
    if(*path == '/') {
        path++;
    }

    memset(iter->path_buf, 0, PATH_MAX);
    strncpy(iter->path_buf, path, PATH_MAX);

    iter->curr_elem = iter->path_buf;
}

char *rm_path_iter_next(RmPathIter *iter) {
    char *elem_begin = iter->curr_elem;

    if(elem_begin && (iter->curr_elem = strchr(elem_begin, '/'))) {
        *(iter->curr_elem) = 0;
        iter->curr_elem += 1;
    }

    return elem_begin;
}

RmNode *rm_trie_insert(RmTrie *self, const char *path, void *value) {
    g_assert(self);
    g_assert(path);

    RmPathIter iter;
    rm_path_iter_init(&iter, path);

    g_mutex_lock(&self->lock);

    char *path_elem = NULL;
    RmNode *curr_node = self->root;

    while((path_elem = rm_path_iter_next(&iter))) {
        curr_node = rm_node_insert(self, curr_node, path_elem);
    }

    if(curr_node != NULL) {
        curr_node->has_value = true;
        curr_node->data = value;
        self->size++;
    }

    g_mutex_unlock(&self->lock);

    return curr_node;
}

RmNode *rm_trie_search_node(RmTrie *self, const char *path) {
    g_assert(self);
    g_assert(path);

    RmPathIter iter;
    rm_path_iter_init(&iter, path);

    g_mutex_lock(&self->lock);

    char *path_elem = NULL;
    RmNode *curr_node = self->root;

    while(curr_node && (path_elem = rm_path_iter_next(&iter))) {
        if(curr_node->children == NULL) {
            /* Can't go any further */
            g_mutex_unlock(&self->lock);
            return NULL;
        }

        curr_node = g_hash_table_lookup(curr_node->children, path_elem);
    }

    g_mutex_unlock(&self->lock);
    return curr_node;
}

void *rm_trie_search(RmTrie *self, const char *path) {
    RmNode *find = rm_trie_search_node(self, path);
    return (find) ? find->data : NULL;
}

bool rm_trie_set_value(RmTrie *self, const char *path, void *data) {
    RmNode *find = rm_trie_search_node(self, path);
    if(find == NULL) {
        return false;
    } else {
        find->data = data;
        return true;
    }
}

char *rm_trie_build_path_unlocked(RmNode *node, char *buf, size_t buf_len) {
    if(node == NULL || node->basename == NULL) {
        return NULL;
    }

    size_t n_elements = 1;
    char *elements[PATH_MAX / 2 + 1] = {node->basename, NULL};

    /* walk up the folder tree, collecting path elements into a list */
    for(RmNode *folder = node->parent; folder && folder->parent;
        folder = folder->parent) {
        elements[n_elements++] = folder->basename;
        if(n_elements >= sizeof(elements)) {
            break;
        }
    }

    /* copy collected elements into *buf */
    char *buf_ptr = buf;
    while(n_elements && (size_t)(buf_ptr - buf) < buf_len) {
        *buf_ptr = '/';
        buf_ptr = g_stpcpy(buf_ptr + 1, (char *)elements[--n_elements]);
    }

    return buf;
}

char *rm_trie_build_path(RmTrie *self, RmNode *node, char *buf, size_t buf_len) {
    if(node == NULL) {
        return NULL;
    }
    char *result = NULL;
    g_mutex_lock(&self->lock);
    { result = rm_trie_build_path_unlocked(node, buf, buf_len); }
    g_mutex_unlock(&self->lock);
    return result;
}

size_t rm_trie_size(RmTrie *self) {
    return self->size;
}

static void _rm_trie_iter(RmTrie *self, RmNode *root, bool pre_order, bool all_nodes,
                          RmTrieIterCallback callback, void *user_data, int level) {
    GHashTableIter iter;
    gpointer key, value;

    if(root == NULL) {
        root = self->root;
    }

    if(pre_order && (all_nodes || root->has_value)) {
        if(callback(self, root, level, user_data) != 0) {
            return;
        }
    }

    if(root->children != NULL) {
        g_hash_table_iter_init(&iter, root->children);
        while(g_hash_table_iter_next(&iter, &key, &value)) {
            _rm_trie_iter(self, value, pre_order, all_nodes, callback, user_data,
                          level + 1);
        }
    }

    if(!pre_order && (all_nodes || root->has_value)) {
        if(callback(self, root, level, user_data) != 0) {
            return;
        }
    }
}

void rm_trie_iter(RmTrie *self, RmNode *root, bool pre_order, bool all_nodes,
                  RmTrieIterCallback callback, void *user_data) {
    g_mutex_lock(&self->lock);
    _rm_trie_iter(self, root, pre_order, all_nodes, callback, user_data, 0);
    g_mutex_unlock(&self->lock);
}

static int rm_trie_destroy_callback(_UNUSED RmTrie *self,
                                    RmNode *node,
                                    _UNUSED int level,
                                    _UNUSED void *user_data) {
    rm_node_free(node);
    return 0;
}

void rm_trie_destroy(RmTrie *self) {
    rm_trie_iter(self, NULL, false, true, rm_trie_destroy_callback, NULL);
    g_string_chunk_free(self->chunks);
    g_mutex_clear(&self->lock);
}

#ifdef _RM_PATHTRICIA_BUILD_MAIN

#include <stdio.h>

static int rm_trie_print_callback(_UNUSED RmTrie *self,
                                  RmNode *node,
                                  int level,
                                  _UNUSED void *user_data) {
    for(int i = 0; i < level; ++i) {
        g_printerr("    ");
    }

    g_printerr("%s %s\n",
               (char *)((node->basename) ? node->basename : "[root]"),
               (node->data) ? "[leaf]" : "");

    return 0;
}

void rm_trie_print(RmTrie *self) {
    rm_trie_iter(self, NULL, true, true, rm_trie_print_callback, NULL);
}

int main(void) {
    RmTrie trie;
    rm_trie_init(&trie);
    GTimer *timer = g_timer_new();

    g_timer_start(timer);
    char buf[1024];
    int i = 0;

    while(fgets(buf, sizeof(buf), stdin)) {
        buf[strlen(buf) - 1] = 0;
        rm_trie_insert(&trie, buf, GUINT_TO_POINTER(++i));
        memset(buf, 0, sizeof(buf));
    }

    g_printerr("Took %2.5f to insert %d items\n", g_timer_elapsed(timer, NULL), i);
    rm_trie_print(&trie);
    memset(buf, 0, sizeof(buf));
    rm_trie_build_path(&trie, rm_trie_search_node(&trie, "/usr/bin/rmlint"), buf,
                       sizeof(buf));
    g_printerr("=> %s\n", buf);

    g_timer_start(timer);
    const int N = 10000000;
    for(int x = 0; x < N; x++) {
        rm_trie_search(&trie, "/usr/bin/rmlint");
    }
    g_printerr("Took %2.5f to search\n", g_timer_elapsed(timer, NULL));

    g_printerr("%u\n", GPOINTER_TO_UINT(rm_trie_search(&trie, "/usr/bin/rmlint")));
    g_printerr("%u\n", GPOINTER_TO_UINT(rm_trie_search(&trie, "/a/b/c")));
    rm_trie_destroy(&trie);
    g_timer_destroy(timer);
    return 0;
}

#endif
