/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Interval trees.
 *
 * Derived from include/linux/interval_tree.h and its dependencies.
 */

#ifndef QEMU_INTERVAL_TREE_H
#define QEMU_INTERVAL_TREE_H

/*
 * For now, don't expose Linux Red-Black Trees separately, but retain the
 * separate type definitions to keep the implementation sane, and allow
 * the possibility of disentangling them later.
 */
typedef struct RBNode
{
    /* Encodes parent with color in the lsb. */
    uintptr_t rb_parent_color;
    struct RBNode *rb_right;
    struct RBNode *rb_left;
} RBNode;

typedef struct RBRoot
{
    RBNode *rb_node;
} RBRoot;

typedef struct RBRootLeftCached {
    RBRoot rb_root;
    RBNode *rb_leftmost;
} RBRootLeftCached;

typedef struct IntervalTreeNode
{
    RBNode rb;

    uint64_t start;    /* Start of interval */
    uint64_t last;     /* Last location _in_ interval */
    uint64_t subtree_last;
} IntervalTreeNode;

typedef RBRootLeftCached IntervalTreeRoot;

/**
 * interval_tree_is_empty
 * @root: root of the tree.
 *
 * Returns true if the tree contains no nodes.
 */
static inline bool interval_tree_is_empty(const IntervalTreeRoot *root)
{
    return root->rb_root.rb_node == NULL;
}

/**
 * interval_tree_insert
 * @node: node to insert,
 * @root: root of the tree.
 *
 * Insert @node into @root, and rebalance.
 */
void interval_tree_insert(IntervalTreeNode *node, IntervalTreeRoot *root);

/**
 * interval_tree_remove
 * @node: node to remove,
 * @root: root of the tree.
 *
 * Remove @node from @root, and rebalance.
 */
void interval_tree_remove(IntervalTreeNode *node, IntervalTreeRoot *root);

/**
 * interval_tree_iter_first:
 * @root: root of the tree,
 * @start, @last: the inclusive interval [start, last].
 *
 * Locate the "first" of a set of nodes within the tree at @root
 * that overlap the interval, where "first" is sorted by start.
 * Returns NULL if no overlap found.
 */
IntervalTreeNode *interval_tree_iter_first(IntervalTreeRoot *root,
                                           uint64_t start, uint64_t last);

/**
 * interval_tree_iter_next:
 * @node: previous search result
 * @start, @last: the inclusive interval [start, last].
 *
 * Locate the "next" of a set of nodes within the tree that overlap the
 * interval; @next is the result of a previous call to
 * interval_tree_iter_{first,next}.  Returns NULL if @next was the last
 * node in the set.
 */
IntervalTreeNode *interval_tree_iter_next(IntervalTreeNode *node,
                                          uint64_t start, uint64_t last);

#endif /* QEMU_INTERVAL_TREE_H */
