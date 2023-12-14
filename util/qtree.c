/*
 * GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * MT safe
 */

#include "qemu/osdep.h"
#include "qemu/qtree.h"

/**
 * SECTION:trees-binary
 * @title: Balanced Binary Trees
 * @short_description: a sorted collection of key/value pairs optimized
 *                     for searching and traversing in order
 *
 * The #QTree structure and its associated functions provide a sorted
 * collection of key/value pairs optimized for searching and traversing
 * in order. This means that most of the operations  (access, search,
 * insertion, deletion, ...) on #QTree are O(log(n)) in average and O(n)
 * in worst case for time complexity. But, note that maintaining a
 * balanced sorted #QTree of n elements is done in time O(n log(n)).
 *
 * To create a new #QTree use q_tree_new().
 *
 * To insert a key/value pair into a #QTree use q_tree_insert()
 * (O(n log(n))).
 *
 * To remove a key/value pair use q_tree_remove() (O(n log(n))).
 *
 * To look up the value corresponding to a given key, use
 * q_tree_lookup() and q_tree_lookup_extended().
 *
 * To find out the number of nodes in a #QTree, use q_tree_nnodes(). To
 * get the height of a #QTree, use q_tree_height().
 *
 * To traverse a #QTree, calling a function for each node visited in
 * the traversal, use q_tree_foreach().
 *
 * To destroy a #QTree, use q_tree_destroy().
 **/

#define MAX_GTREE_HEIGHT 40

/**
 * QTree:
 *
 * The QTree struct is an opaque data structure representing a
 * [balanced binary tree][glib-Balanced-Binary-Trees]. It should be
 * accessed only by using the following functions.
 */
struct _QTree {
    QTreeNode        *root;
    GCompareDataFunc  key_compare;
    GDestroyNotify    key_destroy_func;
    GDestroyNotify    value_destroy_func;
    gpointer          key_compare_data;
    guint             nnodes;
    gint              ref_count;
};

struct _QTreeNode {
    gpointer   key;         /* key for this node */
    gpointer   value;       /* value stored at this node */
    QTreeNode *left;        /* left subtree */
    QTreeNode *right;       /* right subtree */
    gint8      balance;     /* height (right) - height (left) */
    guint8     left_child;
    guint8     right_child;
};


static QTreeNode *q_tree_node_new(gpointer       key,
                                  gpointer       value);
static QTreeNode *q_tree_insert_internal(QTree *tree,
                                         gpointer key,
                                         gpointer value,
                                         gboolean replace);
static gboolean   q_tree_remove_internal(QTree         *tree,
                                         gconstpointer  key,
                                         gboolean       steal);
static QTreeNode *q_tree_node_balance(QTreeNode     *node);
static QTreeNode *q_tree_find_node(QTree         *tree,
                                   gconstpointer  key);
static QTreeNode *q_tree_node_search(QTreeNode *node,
                                     GCompareFunc search_func,
                                     gconstpointer data);
static QTreeNode *q_tree_node_rotate_left(QTreeNode     *node);
static QTreeNode *q_tree_node_rotate_right(QTreeNode     *node);
#ifdef Q_TREE_DEBUG
static void       q_tree_node_check(QTreeNode     *node);
#endif

static QTreeNode*
q_tree_node_new(gpointer key,
                gpointer value)
{
    QTreeNode *node = g_new(QTreeNode, 1);

    node->balance = 0;
    node->left = NULL;
    node->right = NULL;
    node->left_child = FALSE;
    node->right_child = FALSE;
    node->key = key;
    node->value = value;

    return node;
}

/**
 * q_tree_new:
 * @key_compare_func: the function used to order the nodes in the #QTree.
 *   It should return values similar to the standard strcmp() function -
 *   0 if the two arguments are equal, a negative value if the first argument
 *   comes before the second, or a positive value if the first argument comes
 *   after the second.
 *
 * Creates a new #QTree.
 *
 * Returns: a newly allocated #QTree
 */
QTree *
q_tree_new(GCompareFunc key_compare_func)
{
    g_return_val_if_fail(key_compare_func != NULL, NULL);

    return q_tree_new_full((GCompareDataFunc) key_compare_func, NULL,
                           NULL, NULL);
}

/**
 * q_tree_new_with_data:
 * @key_compare_func: qsort()-style comparison function
 * @key_compare_data: data to pass to comparison function
 *
 * Creates a new #QTree with a comparison function that accepts user data.
 * See q_tree_new() for more details.
 *
 * Returns: a newly allocated #QTree
 */
QTree *
q_tree_new_with_data(GCompareDataFunc key_compare_func,
                     gpointer         key_compare_data)
{
    g_return_val_if_fail(key_compare_func != NULL, NULL);

    return q_tree_new_full(key_compare_func, key_compare_data,
                           NULL, NULL);
}

/**
 * q_tree_new_full:
 * @key_compare_func: qsort()-style comparison function
 * @key_compare_data: data to pass to comparison function
 * @key_destroy_func: a function to free the memory allocated for the key
 *   used when removing the entry from the #QTree or %NULL if you don't
 *   want to supply such a function
 * @value_destroy_func: a function to free the memory allocated for the
 *   value used when removing the entry from the #QTree or %NULL if you
 *   don't want to supply such a function
 *
 * Creates a new #QTree like q_tree_new() and allows to specify functions
 * to free the memory allocated for the key and value that get called when
 * removing the entry from the #QTree.
 *
 * Returns: a newly allocated #QTree
 */
QTree *
q_tree_new_full(GCompareDataFunc key_compare_func,
                gpointer         key_compare_data,
                GDestroyNotify   key_destroy_func,
                GDestroyNotify   value_destroy_func)
{
    QTree *tree;

    g_return_val_if_fail(key_compare_func != NULL, NULL);

    tree = g_new(QTree, 1);
    tree->root               = NULL;
    tree->key_compare        = key_compare_func;
    tree->key_destroy_func   = key_destroy_func;
    tree->value_destroy_func = value_destroy_func;
    tree->key_compare_data   = key_compare_data;
    tree->nnodes             = 0;
    tree->ref_count          = 1;

    return tree;
}

/**
 * q_tree_node_first:
 * @tree: a #QTree
 *
 * Returns the first in-order node of the tree, or %NULL
 * for an empty tree.
 *
 * Returns: (nullable) (transfer none): the first node in the tree
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_node_first(QTree *tree)
{
    QTreeNode *tmp;

    g_return_val_if_fail(tree != NULL, NULL);

    if (!tree->root) {
        return NULL;
    }

    tmp = tree->root;

    while (tmp->left_child) {
        tmp = tmp->left;
    }

    return tmp;
}

/**
 * q_tree_node_previous
 * @node: a #QTree node
 *
 * Returns the previous in-order node of the tree, or %NULL
 * if the passed node was already the first one.
 *
 * Returns: (nullable) (transfer none): the previous node in the tree
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_node_previous(QTreeNode *node)
{
    QTreeNode *tmp;

    g_return_val_if_fail(node != NULL, NULL);

    tmp = node->left;

    if (node->left_child) {
        while (tmp->right_child) {
            tmp = tmp->right;
        }
    }

    return tmp;
}

/**
 * q_tree_node_next
 * @node: a #QTree node
 *
 * Returns the next in-order node of the tree, or %NULL
 * if the passed node was already the last one.
 *
 * Returns: (nullable) (transfer none): the next node in the tree
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_node_next(QTreeNode *node)
{
    QTreeNode *tmp;

    g_return_val_if_fail(node != NULL, NULL);

    tmp = node->right;

    if (node->right_child) {
        while (tmp->left_child) {
            tmp = tmp->left;
        }
    }

    return tmp;
}

/**
 * q_tree_remove_all:
 * @tree: a #QTree
 *
 * Removes all nodes from a #QTree and destroys their keys and values,
 * then resets the #QTree’s root to %NULL.
 *
 * Since: 2.70 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static void QEMU_DISABLE_CFI
q_tree_remove_all(QTree *tree)
{
    QTreeNode *node;
    QTreeNode *next;

    g_return_if_fail(tree != NULL);

    node = q_tree_node_first(tree);

    while (node) {
        next = q_tree_node_next(node);

        if (tree->key_destroy_func) {
            tree->key_destroy_func(node->key);
        }
        if (tree->value_destroy_func) {
            tree->value_destroy_func(node->value);
        }
        g_free(node);

#ifdef Q_TREE_DEBUG
        g_assert(tree->nnodes > 0);
        tree->nnodes--;
#endif

        node = next;
    }

#ifdef Q_TREE_DEBUG
    g_assert(tree->nnodes == 0);
#endif

    tree->root = NULL;
#ifndef Q_TREE_DEBUG
    tree->nnodes = 0;
#endif
}

/**
 * q_tree_ref:
 * @tree: a #QTree
 *
 * Increments the reference count of @tree by one.
 *
 * It is safe to call this function from any thread.
 *
 * Returns: the passed in #QTree
 *
 * Since: 2.22
 */
QTree *
q_tree_ref(QTree *tree)
{
    g_return_val_if_fail(tree != NULL, NULL);

    g_atomic_int_inc(&tree->ref_count);

    return tree;
}

/**
 * q_tree_unref:
 * @tree: a #QTree
 *
 * Decrements the reference count of @tree by one.
 * If the reference count drops to 0, all keys and values will
 * be destroyed (if destroy functions were specified) and all
 * memory allocated by @tree will be released.
 *
 * It is safe to call this function from any thread.
 *
 * Since: 2.22
 */
void
q_tree_unref(QTree *tree)
{
    g_return_if_fail(tree != NULL);

    if (g_atomic_int_dec_and_test(&tree->ref_count)) {
        q_tree_remove_all(tree);
        g_free(tree);
    }
}

/**
 * q_tree_destroy:
 * @tree: a #QTree
 *
 * Removes all keys and values from the #QTree and decreases its
 * reference count by one. If keys and/or values are dynamically
 * allocated, you should either free them first or create the #QTree
 * using q_tree_new_full(). In the latter case the destroy functions
 * you supplied will be called on all keys and values before destroying
 * the #QTree.
 */
void
q_tree_destroy(QTree *tree)
{
    g_return_if_fail(tree != NULL);

    q_tree_remove_all(tree);
    q_tree_unref(tree);
}

/**
 * q_tree_insert_node:
 * @tree: a #QTree
 * @key: the key to insert
 * @value: the value corresponding to the key
 *
 * Inserts a key/value pair into a #QTree.
 *
 * If the given key already exists in the #QTree its corresponding value
 * is set to the new value. If you supplied a @value_destroy_func when
 * creating the #QTree, the old value is freed using that function. If
 * you supplied a @key_destroy_func when creating the #QTree, the passed
 * key is freed using that function.
 *
 * The tree is automatically 'balanced' as new key/value pairs are added,
 * so that the distance from the root to every leaf is as small as possible.
 * The cost of maintaining a balanced tree while inserting new key/value
 * result in a O(n log(n)) operation where most of the other operations
 * are O(log(n)).
 *
 * Returns: (transfer none): the inserted (or set) node.
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_insert_node(QTree    *tree,
                   gpointer  key,
                   gpointer  value)
{
    QTreeNode *node;

    g_return_val_if_fail(tree != NULL, NULL);

    node = q_tree_insert_internal(tree, key, value, FALSE);

#ifdef Q_TREE_DEBUG
    q_tree_node_check(tree->root);
#endif

    return node;
}

/**
 * q_tree_insert:
 * @tree: a #QTree
 * @key: the key to insert
 * @value: the value corresponding to the key
 *
 * Inserts a key/value pair into a #QTree.
 *
 * Inserts a new key and value into a #QTree as q_tree_insert_node() does,
 * only this function does not return the inserted or set node.
 */
void
q_tree_insert(QTree    *tree,
              gpointer  key,
              gpointer  value)
{
    q_tree_insert_node(tree, key, value);
}

/**
 * q_tree_replace_node:
 * @tree: a #QTree
 * @key: the key to insert
 * @value: the value corresponding to the key
 *
 * Inserts a new key and value into a #QTree similar to q_tree_insert_node().
 * The difference is that if the key already exists in the #QTree, it gets
 * replaced by the new key. If you supplied a @value_destroy_func when
 * creating the #QTree, the old value is freed using that function. If you
 * supplied a @key_destroy_func when creating the #QTree, the old key is
 * freed using that function.
 *
 * The tree is automatically 'balanced' as new key/value pairs are added,
 * so that the distance from the root to every leaf is as small as possible.
 *
 * Returns: (transfer none): the inserted (or set) node.
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_replace_node(QTree    *tree,
                    gpointer  key,
                    gpointer  value)
{
    QTreeNode *node;

    g_return_val_if_fail(tree != NULL, NULL);

    node = q_tree_insert_internal(tree, key, value, TRUE);

#ifdef Q_TREE_DEBUG
    q_tree_node_check(tree->root);
#endif

    return node;
}

/**
 * q_tree_replace:
 * @tree: a #QTree
 * @key: the key to insert
 * @value: the value corresponding to the key
 *
 * Inserts a new key and value into a #QTree as q_tree_replace_node() does,
 * only this function does not return the inserted or set node.
 */
void
q_tree_replace(QTree    *tree,
               gpointer  key,
               gpointer  value)
{
    q_tree_replace_node(tree, key, value);
}

/* internal insert routine */
static QTreeNode * QEMU_DISABLE_CFI
q_tree_insert_internal(QTree    *tree,
                       gpointer  key,
                       gpointer  value,
                       gboolean  replace)
{
    QTreeNode *node, *retnode;
    QTreeNode *path[MAX_GTREE_HEIGHT];
    int idx;

    g_return_val_if_fail(tree != NULL, NULL);

    if (!tree->root) {
        tree->root = q_tree_node_new(key, value);
        tree->nnodes++;
        return tree->root;
    }

    idx = 0;
    path[idx++] = NULL;
    node = tree->root;

    while (1) {
        int cmp = tree->key_compare(key, node->key, tree->key_compare_data);

        if (cmp == 0) {
            if (tree->value_destroy_func) {
                tree->value_destroy_func(node->value);
            }

            node->value = value;

            if (replace) {
                if (tree->key_destroy_func) {
                    tree->key_destroy_func(node->key);
                }

                node->key = key;
            } else {
                /* free the passed key */
                if (tree->key_destroy_func) {
                    tree->key_destroy_func(key);
                }
            }

            return node;
        } else if (cmp < 0) {
            if (node->left_child) {
                path[idx++] = node;
                node = node->left;
            } else {
                QTreeNode *child = q_tree_node_new(key, value);

                child->left = node->left;
                child->right = node;
                node->left = child;
                node->left_child = TRUE;
                node->balance -= 1;

                tree->nnodes++;

                retnode = child;
                break;
            }
        } else {
            if (node->right_child) {
                path[idx++] = node;
                node = node->right;
            } else {
                QTreeNode *child = q_tree_node_new(key, value);

                child->right = node->right;
                child->left = node;
                node->right = child;
                node->right_child = TRUE;
                node->balance += 1;

                tree->nnodes++;

                retnode = child;
                break;
            }
        }
    }

    /*
     * Restore balance. This is the goodness of a non-recursive
     * implementation, when we are done with balancing we 'break'
     * the loop and we are done.
     */
    while (1) {
        QTreeNode *bparent = path[--idx];
        gboolean left_node = (bparent && node == bparent->left);
        g_assert(!bparent || bparent->left == node || bparent->right == node);

        if (node->balance < -1 || node->balance > 1) {
            node = q_tree_node_balance(node);
            if (bparent == NULL) {
                tree->root = node;
            } else if (left_node) {
                bparent->left = node;
            } else {
                bparent->right = node;
            }
        }

        if (node->balance == 0 || bparent == NULL) {
            break;
        }

        if (left_node) {
            bparent->balance -= 1;
        } else {
            bparent->balance += 1;
        }

        node = bparent;
    }

    return retnode;
}

/**
 * q_tree_remove:
 * @tree: a #QTree
 * @key: the key to remove
 *
 * Removes a key/value pair from a #QTree.
 *
 * If the #QTree was created using q_tree_new_full(), the key and value
 * are freed using the supplied destroy functions, otherwise you have to
 * make sure that any dynamically allocated values are freed yourself.
 * If the key does not exist in the #QTree, the function does nothing.
 *
 * The cost of maintaining a balanced tree while removing a key/value
 * result in a O(n log(n)) operation where most of the other operations
 * are O(log(n)).
 *
 * Returns: %TRUE if the key was found (prior to 2.8, this function
 *     returned nothing)
 */
gboolean
q_tree_remove(QTree         *tree,
              gconstpointer  key)
{
    gboolean removed;

    g_return_val_if_fail(tree != NULL, FALSE);

    removed = q_tree_remove_internal(tree, key, FALSE);

#ifdef Q_TREE_DEBUG
    q_tree_node_check(tree->root);
#endif

    return removed;
}

/**
 * q_tree_steal:
 * @tree: a #QTree
 * @key: the key to remove
 *
 * Removes a key and its associated value from a #QTree without calling
 * the key and value destroy functions.
 *
 * If the key does not exist in the #QTree, the function does nothing.
 *
 * Returns: %TRUE if the key was found (prior to 2.8, this function
 *     returned nothing)
 */
gboolean
q_tree_steal(QTree         *tree,
             gconstpointer  key)
{
    gboolean removed;

    g_return_val_if_fail(tree != NULL, FALSE);

    removed = q_tree_remove_internal(tree, key, TRUE);

#ifdef Q_TREE_DEBUG
    q_tree_node_check(tree->root);
#endif

    return removed;
}

/* internal remove routine */
static gboolean QEMU_DISABLE_CFI
q_tree_remove_internal(QTree         *tree,
                       gconstpointer  key,
                       gboolean       steal)
{
    QTreeNode *node, *parent, *balance;
    QTreeNode *path[MAX_GTREE_HEIGHT];
    int idx;
    gboolean left_node;

    g_return_val_if_fail(tree != NULL, FALSE);

    if (!tree->root) {
        return FALSE;
    }

    idx = 0;
    path[idx++] = NULL;
    node = tree->root;

    while (1) {
        int cmp = tree->key_compare(key, node->key, tree->key_compare_data);

        if (cmp == 0) {
            break;
        } else if (cmp < 0) {
            if (!node->left_child) {
                return FALSE;
            }

            path[idx++] = node;
            node = node->left;
        } else {
            if (!node->right_child) {
                return FALSE;
            }

            path[idx++] = node;
            node = node->right;
        }
    }

    /*
     * The following code is almost equal to q_tree_remove_node,
     * except that we do not have to call q_tree_node_parent.
     */
    balance = parent = path[--idx];
    g_assert(!parent || parent->left == node || parent->right == node);
    left_node = (parent && node == parent->left);

    if (!node->left_child) {
        if (!node->right_child) {
            if (!parent) {
                tree->root = NULL;
            } else if (left_node) {
                parent->left_child = FALSE;
                parent->left = node->left;
                parent->balance += 1;
            } else {
                parent->right_child = FALSE;
                parent->right = node->right;
                parent->balance -= 1;
            }
        } else {
            /* node has a right child */
            QTreeNode *tmp = q_tree_node_next(node);
            tmp->left = node->left;

            if (!parent) {
                tree->root = node->right;
            } else if (left_node) {
                parent->left = node->right;
                parent->balance += 1;
            } else {
                parent->right = node->right;
                parent->balance -= 1;
            }
        }
    } else {
        /* node has a left child */
        if (!node->right_child) {
            QTreeNode *tmp = q_tree_node_previous(node);
            tmp->right = node->right;

            if (parent == NULL) {
                tree->root = node->left;
            } else if (left_node) {
                parent->left = node->left;
                parent->balance += 1;
            } else {
                parent->right = node->left;
                parent->balance -= 1;
            }
        } else {
            /* node has a both children (pant, pant!) */
            QTreeNode *prev = node->left;
            QTreeNode *next = node->right;
            QTreeNode *nextp = node;
            int old_idx = idx + 1;
            idx++;

            /* path[idx] == parent */
            /* find the immediately next node (and its parent) */
            while (next->left_child) {
                path[++idx] = nextp = next;
                next = next->left;
            }

            path[old_idx] = next;
            balance = path[idx];

            /* remove 'next' from the tree */
            if (nextp != node) {
                if (next->right_child) {
                    nextp->left = next->right;
                } else {
                    nextp->left_child = FALSE;
                }
                nextp->balance += 1;

                next->right_child = TRUE;
                next->right = node->right;
            } else {
                node->balance -= 1;
            }

            /* set the prev to point to the right place */
            while (prev->right_child) {
                prev = prev->right;
            }
            prev->right = next;

            /* prepare 'next' to replace 'node' */
            next->left_child = TRUE;
            next->left = node->left;
            next->balance = node->balance;

            if (!parent) {
                tree->root = next;
            } else if (left_node) {
                parent->left = next;
            } else {
                parent->right = next;
            }
        }
    }

    /* restore balance */
    if (balance) {
        while (1) {
            QTreeNode *bparent = path[--idx];
            g_assert(!bparent ||
                     bparent->left == balance ||
                     bparent->right == balance);
            left_node = (bparent && balance == bparent->left);

            if (balance->balance < -1 || balance->balance > 1) {
                balance = q_tree_node_balance(balance);
                if (!bparent) {
                    tree->root = balance;
                } else if (left_node) {
                    bparent->left = balance;
                } else {
                    bparent->right = balance;
                }
            }

            if (balance->balance != 0 || !bparent) {
                break;
            }

            if (left_node) {
                bparent->balance += 1;
            } else {
                bparent->balance -= 1;
            }

            balance = bparent;
        }
    }

    if (!steal) {
        if (tree->key_destroy_func) {
            tree->key_destroy_func(node->key);
        }
        if (tree->value_destroy_func) {
            tree->value_destroy_func(node->value);
        }
    }

    g_free(node);

    tree->nnodes--;

    return TRUE;
}

/**
 * q_tree_lookup_node:
 * @tree: a #QTree
 * @key: the key to look up
 *
 * Gets the tree node corresponding to the given key. Since a #QTree is
 * automatically balanced as key/value pairs are added, key lookup
 * is O(log n) (where n is the number of key/value pairs in the tree).
 *
 * Returns: (nullable) (transfer none): the tree node corresponding to
 *          the key, or %NULL if the key was not found
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_lookup_node(QTree         *tree,
                   gconstpointer  key)
{
    g_return_val_if_fail(tree != NULL, NULL);

    return q_tree_find_node(tree, key);
}

/**
 * q_tree_lookup:
 * @tree: a #QTree
 * @key: the key to look up
 *
 * Gets the value corresponding to the given key. Since a #QTree is
 * automatically balanced as key/value pairs are added, key lookup
 * is O(log n) (where n is the number of key/value pairs in the tree).
 *
 * Returns: the value corresponding to the key, or %NULL
 *     if the key was not found
 */
gpointer
q_tree_lookup(QTree         *tree,
              gconstpointer  key)
{
    QTreeNode *node;

    node = q_tree_lookup_node(tree, key);

    return node ? node->value : NULL;
}

/**
 * q_tree_lookup_extended:
 * @tree: a #QTree
 * @lookup_key: the key to look up
 * @orig_key: (out) (optional) (nullable): returns the original key
 * @value: (out) (optional) (nullable): returns the value associated with
 *         the key
 *
 * Looks up a key in the #QTree, returning the original key and the
 * associated value. This is useful if you need to free the memory
 * allocated for the original key, for example before calling
 * q_tree_remove().
 *
 * Returns: %TRUE if the key was found in the #QTree
 */
gboolean
q_tree_lookup_extended(QTree         *tree,
                       gconstpointer  lookup_key,
                       gpointer      *orig_key,
                       gpointer      *value)
{
    QTreeNode *node;

    g_return_val_if_fail(tree != NULL, FALSE);

    node = q_tree_find_node(tree, lookup_key);

    if (node) {
        if (orig_key) {
            *orig_key = node->key;
        }
        if (value) {
            *value = node->value;
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

/**
 * q_tree_foreach:
 * @tree: a #QTree
 * @func: the function to call for each node visited.
 *     If this function returns %TRUE, the traversal is stopped.
 * @user_data: user data to pass to the function
 *
 * Calls the given function for each of the key/value pairs in the #QTree.
 * The function is passed the key and value of each pair, and the given
 * @data parameter. The tree is traversed in sorted order.
 *
 * The tree may not be modified while iterating over it (you can't
 * add/remove items). To remove all items matching a predicate, you need
 * to add each item to a list in your #GTraverseFunc as you walk over
 * the tree, then walk the list and remove each item.
 */
void
q_tree_foreach(QTree         *tree,
               GTraverseFunc  func,
               gpointer       user_data)
{
    QTreeNode *node;

    g_return_if_fail(tree != NULL);

    if (!tree->root) {
        return;
    }

    node = q_tree_node_first(tree);

    while (node) {
        if ((*func)(node->key, node->value, user_data)) {
            break;
        }

        node = q_tree_node_next(node);
    }
}

/**
 * q_tree_search_node:
 * @tree: a #QTree
 * @search_func: a function used to search the #QTree
 * @user_data: the data passed as the second argument to @search_func
 *
 * Searches a #QTree using @search_func.
 *
 * The @search_func is called with a pointer to the key of a key/value
 * pair in the tree, and the passed in @user_data. If @search_func returns
 * 0 for a key/value pair, then the corresponding node is returned as
 * the result of q_tree_search(). If @search_func returns -1, searching
 * will proceed among the key/value pairs that have a smaller key; if
 * @search_func returns 1, searching will proceed among the key/value
 * pairs that have a larger key.
 *
 * Returns: (nullable) (transfer none): the node corresponding to the
 *          found key, or %NULL if the key was not found
 *
 * Since: 2.68 in GLib. Internal in Qtree, i.e. not in the public API.
 */
static QTreeNode *
q_tree_search_node(QTree         *tree,
                   GCompareFunc   search_func,
                   gconstpointer  user_data)
{
    g_return_val_if_fail(tree != NULL, NULL);

    if (!tree->root) {
        return NULL;
    }

    return q_tree_node_search(tree->root, search_func, user_data);
}

/**
 * q_tree_search:
 * @tree: a #QTree
 * @search_func: a function used to search the #QTree
 * @user_data: the data passed as the second argument to @search_func
 *
 * Searches a #QTree using @search_func.
 *
 * The @search_func is called with a pointer to the key of a key/value
 * pair in the tree, and the passed in @user_data. If @search_func returns
 * 0 for a key/value pair, then the corresponding value is returned as
 * the result of q_tree_search(). If @search_func returns -1, searching
 * will proceed among the key/value pairs that have a smaller key; if
 * @search_func returns 1, searching will proceed among the key/value
 * pairs that have a larger key.
 *
 * Returns: the value corresponding to the found key, or %NULL
 *     if the key was not found
 */
gpointer
q_tree_search(QTree         *tree,
              GCompareFunc   search_func,
              gconstpointer  user_data)
{
    QTreeNode *node;

    node = q_tree_search_node(tree, search_func, user_data);

    return node ? node->value : NULL;
}

/**
 * q_tree_height:
 * @tree: a #QTree
 *
 * Gets the height of a #QTree.
 *
 * If the #QTree contains no nodes, the height is 0.
 * If the #QTree contains only one root node the height is 1.
 * If the root node has children the height is 2, etc.
 *
 * Returns: the height of @tree
 */
gint
q_tree_height(QTree *tree)
{
    QTreeNode *node;
    gint height;

    g_return_val_if_fail(tree != NULL, 0);

    if (!tree->root) {
        return 0;
    }

    height = 0;
    node = tree->root;

    while (1) {
        height += 1 + MAX(node->balance, 0);

        if (!node->left_child) {
            return height;
        }

        node = node->left;
    }
}

/**
 * q_tree_nnodes:
 * @tree: a #QTree
 *
 * Gets the number of nodes in a #QTree.
 *
 * Returns: the number of nodes in @tree
 */
gint
q_tree_nnodes(QTree *tree)
{
    g_return_val_if_fail(tree != NULL, 0);

    return tree->nnodes;
}

static QTreeNode *
q_tree_node_balance(QTreeNode *node)
{
    if (node->balance < -1) {
        if (node->left->balance > 0) {
            node->left = q_tree_node_rotate_left(node->left);
        }
        node = q_tree_node_rotate_right(node);
    } else if (node->balance > 1) {
        if (node->right->balance < 0) {
            node->right = q_tree_node_rotate_right(node->right);
        }
        node = q_tree_node_rotate_left(node);
    }

    return node;
}

static QTreeNode * QEMU_DISABLE_CFI
q_tree_find_node(QTree        *tree,
                 gconstpointer key)
{
    QTreeNode *node;
    gint cmp;

    node = tree->root;
    if (!node) {
        return NULL;
    }

    while (1) {
        cmp = tree->key_compare(key, node->key, tree->key_compare_data);
        if (cmp == 0) {
            return node;
        } else if (cmp < 0) {
            if (!node->left_child) {
                return NULL;
            }

            node = node->left;
        } else {
            if (!node->right_child) {
                return NULL;
            }

            node = node->right;
        }
    }
}

static QTreeNode *
q_tree_node_search(QTreeNode     *node,
                   GCompareFunc   search_func,
                   gconstpointer  data)
{
    gint dir;

    if (!node) {
        return NULL;
    }

    while (1) {
        dir = (*search_func)(node->key, data);
        if (dir == 0) {
            return node;
        } else if (dir < 0) {
            if (!node->left_child) {
                return NULL;
            }

            node = node->left;
        } else {
            if (!node->right_child) {
                return NULL;
            }

            node = node->right;
        }
    }
}

static QTreeNode *
q_tree_node_rotate_left(QTreeNode *node)
{
    QTreeNode *right;
    gint a_bal;
    gint b_bal;

    right = node->right;

    if (right->left_child) {
        node->right = right->left;
    } else {
        node->right_child = FALSE;
        right->left_child = TRUE;
    }
    right->left = node;

    a_bal = node->balance;
    b_bal = right->balance;

    if (b_bal <= 0) {
        if (a_bal >= 1) {
            right->balance = b_bal - 1;
        } else {
            right->balance = a_bal + b_bal - 2;
        }
        node->balance = a_bal - 1;
    } else {
        if (a_bal <= b_bal) {
            right->balance = a_bal - 2;
        } else {
            right->balance = b_bal - 1;
        }
        node->balance = a_bal - b_bal - 1;
    }

    return right;
}

static QTreeNode *
q_tree_node_rotate_right(QTreeNode *node)
{
    QTreeNode *left;
    gint a_bal;
    gint b_bal;

    left = node->left;

    if (left->right_child) {
        node->left = left->right;
    } else {
        node->left_child = FALSE;
        left->right_child = TRUE;
    }
    left->right = node;

    a_bal = node->balance;
    b_bal = left->balance;

    if (b_bal <= 0) {
        if (b_bal > a_bal) {
            left->balance = b_bal + 1;
        } else {
            left->balance = a_bal + 2;
        }
        node->balance = a_bal - b_bal + 1;
    } else {
        if (a_bal <= -1) {
            left->balance = b_bal + 1;
        } else {
            left->balance = a_bal + b_bal + 2;
        }
        node->balance = a_bal + 1;
    }

    return left;
}

#ifdef Q_TREE_DEBUG
static gint
q_tree_node_height(QTreeNode *node)
{
    gint left_height;
    gint right_height;

    if (node) {
        left_height = 0;
        right_height = 0;

        if (node->left_child) {
            left_height = q_tree_node_height(node->left);
        }

        if (node->right_child) {
            right_height = q_tree_node_height(node->right);
        }

        return MAX(left_height, right_height) + 1;
    }

    return 0;
}

static void q_tree_node_check(QTreeNode *node)
{
    gint left_height;
    gint right_height;
    gint balance;
    QTreeNode *tmp;

    if (node) {
        if (node->left_child) {
            tmp = q_tree_node_previous(node);
            g_assert(tmp->right == node);
        }

        if (node->right_child) {
            tmp = q_tree_node_next(node);
            g_assert(tmp->left == node);
        }

        left_height = 0;
        right_height = 0;

        if (node->left_child) {
            left_height = q_tree_node_height(node->left);
        }
        if (node->right_child) {
            right_height = q_tree_node_height(node->right);
        }

        balance = right_height - left_height;
        g_assert(balance == node->balance);

        if (node->left_child) {
            q_tree_node_check(node->left);
        }
        if (node->right_child) {
            q_tree_node_check(node->right);
        }
    }
}
#endif
