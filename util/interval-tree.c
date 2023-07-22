/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/interval-tree.h"
#include "qemu/atomic.h"

/*
 * Red Black Trees.
 *
 * For now, don't expose Linux Red-Black Trees separately, but retain the
 * separate type definitions to keep the implementation sane, and allow
 * the possibility of separating them later.
 *
 * Derived from include/linux/rbtree_augmented.h and its dependencies.
 */

/*
 * red-black trees properties:  https://en.wikipedia.org/wiki/Rbtree
 *
 *  1) A node is either red or black
 *  2) The root is black
 *  3) All leaves (NULL) are black
 *  4) Both children of every red node are black
 *  5) Every simple path from root to leaves contains the same number
 *     of black nodes.
 *
 *  4 and 5 give the O(log n) guarantee, since 4 implies you cannot have two
 *  consecutive red nodes in a path and every red node is therefore followed by
 *  a black. So if B is the number of black nodes on every simple path (as per
 *  5), then the longest possible path due to 4 is 2B.
 *
 *  We shall indicate color with case, where black nodes are uppercase and red
 *  nodes will be lowercase. Unknown color nodes shall be drawn as red within
 *  parentheses and have some accompanying text comment.
 *
 * Notes on lockless lookups:
 *
 * All stores to the tree structure (rb_left and rb_right) must be done using
 * WRITE_ONCE [qatomic_set for QEMU]. And we must not inadvertently cause
 * (temporary) loops in the tree structure as seen in program order.
 *
 * These two requirements will allow lockless iteration of the tree -- not
 * correct iteration mind you, tree rotations are not atomic so a lookup might
 * miss entire subtrees.
 *
 * But they do guarantee that any such traversal will only see valid elements
 * and that it will indeed complete -- does not get stuck in a loop.
 *
 * It also guarantees that if the lookup returns an element it is the 'correct'
 * one. But not returning an element does _NOT_ mean it's not present.
 *
 * NOTE:
 *
 * Stores to __rb_parent_color are not important for simple lookups so those
 * are left undone as of now. Nor did I check for loops involving parent
 * pointers.
 */

typedef enum RBColor
{
    RB_RED,
    RB_BLACK,
} RBColor;

typedef struct RBAugmentCallbacks {
    void (*propagate)(RBNode *node, RBNode *stop);
    void (*copy)(RBNode *old, RBNode *new);
    void (*rotate)(RBNode *old, RBNode *new);
} RBAugmentCallbacks;

static inline RBNode *rb_parent(const RBNode *n)
{
    return (RBNode *)(n->rb_parent_color & ~1);
}

static inline RBNode *rb_red_parent(const RBNode *n)
{
    return (RBNode *)n->rb_parent_color;
}

static inline RBColor pc_color(uintptr_t pc)
{
    return (RBColor)(pc & 1);
}

static inline bool pc_is_red(uintptr_t pc)
{
    return pc_color(pc) == RB_RED;
}

static inline bool pc_is_black(uintptr_t pc)
{
    return !pc_is_red(pc);
}

static inline RBColor rb_color(const RBNode *n)
{
    return pc_color(n->rb_parent_color);
}

static inline bool rb_is_red(const RBNode *n)
{
    return pc_is_red(n->rb_parent_color);
}

static inline bool rb_is_black(const RBNode *n)
{
    return pc_is_black(n->rb_parent_color);
}

static inline void rb_set_black(RBNode *n)
{
    n->rb_parent_color |= RB_BLACK;
}

static inline void rb_set_parent_color(RBNode *n, RBNode *p, RBColor color)
{
    n->rb_parent_color = (uintptr_t)p | color;
}

static inline void rb_set_parent(RBNode *n, RBNode *p)
{
    rb_set_parent_color(n, p, rb_color(n));
}

static inline void rb_link_node(RBNode *node, RBNode *parent, RBNode **rb_link)
{
    node->rb_parent_color = (uintptr_t)parent;
    node->rb_left = node->rb_right = NULL;

    /*
     * Ensure that node is initialized before insertion,
     * as viewed by a concurrent search.
     */
    qatomic_mb_set(rb_link, node);
}

static RBNode *rb_next(RBNode *node)
{
    RBNode *parent;

    /* OMIT: if empty node, return null. */

    /*
     * If we have a right-hand child, go down and then left as far as we can.
     */
    if (node->rb_right) {
        node = node->rb_right;
        while (node->rb_left) {
            node = node->rb_left;
        }
        return node;
    }

    /*
     * No right-hand children. Everything down and left is smaller than us,
     * so any 'next' node must be in the general direction of our parent.
     * Go up the tree; any time the ancestor is a right-hand child of its
     * parent, keep going up. First time it's a left-hand child of its
     * parent, said parent is our 'next' node.
     */
    while ((parent = rb_parent(node)) && node == parent->rb_right) {
        node = parent;
    }

    return parent;
}

static inline void rb_change_child(RBNode *old, RBNode *new,
                                   RBNode *parent, RBRoot *root)
{
    if (!parent) {
        qatomic_set(&root->rb_node, new);
    } else if (parent->rb_left == old) {
        qatomic_set(&parent->rb_left, new);
    } else {
        qatomic_set(&parent->rb_right, new);
    }
}

static inline void rb_rotate_set_parents(RBNode *old, RBNode *new,
                                         RBRoot *root, RBColor color)
{
    RBNode *parent = rb_parent(old);

    new->rb_parent_color = old->rb_parent_color;
    rb_set_parent_color(old, new, color);
    rb_change_child(old, new, parent, root);
}

static void rb_insert_augmented(RBNode *node, RBRoot *root,
                                const RBAugmentCallbacks *augment)
{
    RBNode *parent = rb_red_parent(node), *gparent, *tmp;

    while (true) {
        /*
         * Loop invariant: node is red.
         */
        if (unlikely(!parent)) {
            /*
             * The inserted node is root. Either this is the first node, or
             * we recursed at Case 1 below and are no longer violating 4).
             */
            rb_set_parent_color(node, NULL, RB_BLACK);
            break;
        }

        /*
         * If there is a black parent, we are done.  Otherwise, take some
         * corrective action as, per 4), we don't want a red root or two
         * consecutive red nodes.
         */
        if (rb_is_black(parent)) {
            break;
        }

        gparent = rb_red_parent(parent);

        tmp = gparent->rb_right;
        if (parent != tmp) {    /* parent == gparent->rb_left */
            if (tmp && rb_is_red(tmp)) {
                /*
                 * Case 1 - node's uncle is red (color flips).
                 *
                 *       G            g
                 *      / \          / \
                 *     p   u  -->   P   U
                 *    /            /
                 *   n            n
                 *
                 * However, since g's parent might be red, and 4) does not
                 * allow this, we need to recurse at g.
                 */
                rb_set_parent_color(tmp, gparent, RB_BLACK);
                rb_set_parent_color(parent, gparent, RB_BLACK);
                node = gparent;
                parent = rb_parent(node);
                rb_set_parent_color(node, parent, RB_RED);
                continue;
            }

            tmp = parent->rb_right;
            if (node == tmp) {
                /*
                 * Case 2 - node's uncle is black and node is
                 * the parent's right child (left rotate at parent).
                 *
                 *      G             G
                 *     / \           / \
                 *    p   U  -->    n   U
                 *     \           /
                 *      n         p
                 *
                 * This still leaves us in violation of 4), the
                 * continuation into Case 3 will fix that.
                 */
                tmp = node->rb_left;
                qatomic_set(&parent->rb_right, tmp);
                qatomic_set(&node->rb_left, parent);
                if (tmp) {
                    rb_set_parent_color(tmp, parent, RB_BLACK);
                }
                rb_set_parent_color(parent, node, RB_RED);
                augment->rotate(parent, node);
                parent = node;
                tmp = node->rb_right;
            }

            /*
             * Case 3 - node's uncle is black and node is
             * the parent's left child (right rotate at gparent).
             *
             *        G           P
             *       / \         / \
             *      p   U  -->  n   g
             *     /                 \
             *    n                   U
             */
            qatomic_set(&gparent->rb_left, tmp); /* == parent->rb_right */
            qatomic_set(&parent->rb_right, gparent);
            if (tmp) {
                rb_set_parent_color(tmp, gparent, RB_BLACK);
            }
            rb_rotate_set_parents(gparent, parent, root, RB_RED);
            augment->rotate(gparent, parent);
            break;
        } else {
            tmp = gparent->rb_left;
            if (tmp && rb_is_red(tmp)) {
                /* Case 1 - color flips */
                rb_set_parent_color(tmp, gparent, RB_BLACK);
                rb_set_parent_color(parent, gparent, RB_BLACK);
                node = gparent;
                parent = rb_parent(node);
                rb_set_parent_color(node, parent, RB_RED);
                continue;
            }

            tmp = parent->rb_left;
            if (node == tmp) {
                /* Case 2 - right rotate at parent */
                tmp = node->rb_right;
                qatomic_set(&parent->rb_left, tmp);
                qatomic_set(&node->rb_right, parent);
                if (tmp) {
                    rb_set_parent_color(tmp, parent, RB_BLACK);
                }
                rb_set_parent_color(parent, node, RB_RED);
                augment->rotate(parent, node);
                parent = node;
                tmp = node->rb_left;
            }

            /* Case 3 - left rotate at gparent */
            qatomic_set(&gparent->rb_right, tmp); /* == parent->rb_left */
            qatomic_set(&parent->rb_left, gparent);
            if (tmp) {
                rb_set_parent_color(tmp, gparent, RB_BLACK);
            }
            rb_rotate_set_parents(gparent, parent, root, RB_RED);
            augment->rotate(gparent, parent);
            break;
        }
    }
}

static void rb_insert_augmented_cached(RBNode *node,
                                       RBRootLeftCached *root, bool newleft,
                                       const RBAugmentCallbacks *augment)
{
    if (newleft) {
        root->rb_leftmost = node;
    }
    rb_insert_augmented(node, &root->rb_root, augment);
}

static void rb_erase_color(RBNode *parent, RBRoot *root,
                           const RBAugmentCallbacks *augment)
{
    RBNode *node = NULL, *sibling, *tmp1, *tmp2;

    while (true) {
        /*
         * Loop invariants:
         * - node is black (or NULL on first iteration)
         * - node is not the root (parent is not NULL)
         * - All leaf paths going through parent and node have a
         *   black node count that is 1 lower than other leaf paths.
         */
        sibling = parent->rb_right;
        if (node != sibling) {  /* node == parent->rb_left */
            if (rb_is_red(sibling)) {
                /*
                 * Case 1 - left rotate at parent
                 *
                 *     P               S
                 *    / \             / \ 
                 *   N   s    -->    p   Sr
                 *      / \         / \ 
                 *     Sl  Sr      N   Sl
                 */
                tmp1 = sibling->rb_left;
                qatomic_set(&parent->rb_right, tmp1);
                qatomic_set(&sibling->rb_left, parent);
                rb_set_parent_color(tmp1, parent, RB_BLACK);
                rb_rotate_set_parents(parent, sibling, root, RB_RED);
                augment->rotate(parent, sibling);
                sibling = tmp1;
            }
            tmp1 = sibling->rb_right;
            if (!tmp1 || rb_is_black(tmp1)) {
                tmp2 = sibling->rb_left;
                if (!tmp2 || rb_is_black(tmp2)) {
                    /*
                     * Case 2 - sibling color flip
                     * (p could be either color here)
                     *
                     *    (p)           (p)
                     *    / \           / \ 
                     *   N   S    -->  N   s
                     *      / \           / \ 
                     *     Sl  Sr        Sl  Sr
                     *
                     * This leaves us violating 5) which
                     * can be fixed by flipping p to black
                     * if it was red, or by recursing at p.
                     * p is red when coming from Case 1.
                     */
                    rb_set_parent_color(sibling, parent, RB_RED);
                    if (rb_is_red(parent)) {
                        rb_set_black(parent);
                    } else {
                        node = parent;
                        parent = rb_parent(node);
                        if (parent) {
                            continue;
                        }
                    }
                    break;
                }
                /*
                 * Case 3 - right rotate at sibling
                 * (p could be either color here)
                 *
                 *   (p)           (p)
                 *   / \           / \
                 *  N   S    -->  N   sl
                 *     / \             \
                 *    sl  Sr            S
                 *                       \
                 *                        Sr
                 *
                 * Note: p might be red, and then bot
                 * p and sl are red after rotation (which
                 * breaks property 4). This is fixed in
                 * Case 4 (in rb_rotate_set_parents()
                 *         which set sl the color of p
                 *         and set p RB_BLACK)
                 *
                 *   (p)            (sl)
                 *   / \            /  \
                 *  N   sl   -->   P    S
                 *       \        /      \
                 *        S      N        Sr
                 *         \
                 *          Sr
                 */
                tmp1 = tmp2->rb_right;
                qatomic_set(&sibling->rb_left, tmp1);
                qatomic_set(&tmp2->rb_right, sibling);
                qatomic_set(&parent->rb_right, tmp2);
                if (tmp1) {
                    rb_set_parent_color(tmp1, sibling, RB_BLACK);
                }
                augment->rotate(sibling, tmp2);
                tmp1 = sibling;
                sibling = tmp2;
            }
            /*
             * Case 4 - left rotate at parent + color flips
             * (p and sl could be either color here.
             *  After rotation, p becomes black, s acquires
             *  p's color, and sl keeps its color)
             *
             *      (p)             (s)
             *      / \             / \
             *     N   S     -->   P   Sr
             *        / \         / \
             *      (sl) sr      N  (sl)
             */
            tmp2 = sibling->rb_left;
            qatomic_set(&parent->rb_right, tmp2);
            qatomic_set(&sibling->rb_left, parent);
            rb_set_parent_color(tmp1, sibling, RB_BLACK);
            if (tmp2) {
                rb_set_parent(tmp2, parent);
            }
            rb_rotate_set_parents(parent, sibling, root, RB_BLACK);
            augment->rotate(parent, sibling);
            break;
        } else {
            sibling = parent->rb_left;
            if (rb_is_red(sibling)) {
                /* Case 1 - right rotate at parent */
                tmp1 = sibling->rb_right;
                qatomic_set(&parent->rb_left, tmp1);
                qatomic_set(&sibling->rb_right, parent);
                rb_set_parent_color(tmp1, parent, RB_BLACK);
                rb_rotate_set_parents(parent, sibling, root, RB_RED);
                augment->rotate(parent, sibling);
                sibling = tmp1;
            }
            tmp1 = sibling->rb_left;
            if (!tmp1 || rb_is_black(tmp1)) {
                tmp2 = sibling->rb_right;
                if (!tmp2 || rb_is_black(tmp2)) {
                    /* Case 2 - sibling color flip */
                    rb_set_parent_color(sibling, parent, RB_RED);
                    if (rb_is_red(parent)) {
                        rb_set_black(parent);
                    } else {
                        node = parent;
                        parent = rb_parent(node);
                        if (parent) {
                            continue;
                        }
                    }
                    break;
                }
                /* Case 3 - left rotate at sibling */
                tmp1 = tmp2->rb_left;
                qatomic_set(&sibling->rb_right, tmp1);
                qatomic_set(&tmp2->rb_left, sibling);
                qatomic_set(&parent->rb_left, tmp2);
                if (tmp1) {
                    rb_set_parent_color(tmp1, sibling, RB_BLACK);
                }
                augment->rotate(sibling, tmp2);
                tmp1 = sibling;
                sibling = tmp2;
            }
            /* Case 4 - right rotate at parent + color flips */
            tmp2 = sibling->rb_right;
            qatomic_set(&parent->rb_left, tmp2);
            qatomic_set(&sibling->rb_right, parent);
            rb_set_parent_color(tmp1, sibling, RB_BLACK);
            if (tmp2) {
                rb_set_parent(tmp2, parent);
            }
            rb_rotate_set_parents(parent, sibling, root, RB_BLACK);
            augment->rotate(parent, sibling);
            break;
        }
    }
}

static void rb_erase_augmented(RBNode *node, RBRoot *root,
                               const RBAugmentCallbacks *augment)
{
    RBNode *child = node->rb_right;
    RBNode *tmp = node->rb_left;
    RBNode *parent, *rebalance;
    uintptr_t pc;

    if (!tmp) {
        /*
         * Case 1: node to erase has no more than 1 child (easy!)
         *
         * Note that if there is one child it must be red due to 5)
         * and node must be black due to 4). We adjust colors locally
         * so as to bypass rb_erase_color() later on.
         */
        pc = node->rb_parent_color;
        parent = rb_parent(node);
        rb_change_child(node, child, parent, root);
        if (child) {
            child->rb_parent_color = pc;
            rebalance = NULL;
        } else {
            rebalance = pc_is_black(pc) ? parent : NULL;
        }
        tmp = parent;
    } else if (!child) {
        /* Still case 1, but this time the child is node->rb_left */
        pc = node->rb_parent_color;
        parent = rb_parent(node);
        tmp->rb_parent_color = pc;
        rb_change_child(node, tmp, parent, root);
        rebalance = NULL;
        tmp = parent;
    } else {
        RBNode *successor = child, *child2;
        tmp = child->rb_left;
        if (!tmp) {
            /*
             * Case 2: node's successor is its right child
             *
             *    (n)          (s)
             *    / \          / \
             *  (x) (s)  ->  (x) (c)
             *        \
             *        (c)
             */
            parent = successor;
            child2 = successor->rb_right;

            augment->copy(node, successor);
        } else {
            /*
             * Case 3: node's successor is leftmost under
             * node's right child subtree
             *
             *    (n)          (s)
             *    / \          / \
             *  (x) (y)  ->  (x) (y)
             *      /            /
             *    (p)          (p)
             *    /            /
             *  (s)          (c)
             *    \
             *    (c)
             */
            do {
                parent = successor;
                successor = tmp;
                tmp = tmp->rb_left;
            } while (tmp);
            child2 = successor->rb_right;
            qatomic_set(&parent->rb_left, child2);
            qatomic_set(&successor->rb_right, child);
            rb_set_parent(child, successor);

            augment->copy(node, successor);
            augment->propagate(parent, successor);
        }

        tmp = node->rb_left;
        qatomic_set(&successor->rb_left, tmp);
        rb_set_parent(tmp, successor);

        pc = node->rb_parent_color;
        tmp = rb_parent(node);
        rb_change_child(node, successor, tmp, root);

        if (child2) {
            rb_set_parent_color(child2, parent, RB_BLACK);
            rebalance = NULL;
        } else {
            rebalance = rb_is_black(successor) ? parent : NULL;
        }
        successor->rb_parent_color = pc;
        tmp = successor;
    }

    augment->propagate(tmp, NULL);

    if (rebalance) {
        rb_erase_color(rebalance, root, augment);
    }
}

static void rb_erase_augmented_cached(RBNode *node, RBRootLeftCached *root,
                                      const RBAugmentCallbacks *augment)
{
    if (root->rb_leftmost == node) {
        root->rb_leftmost = rb_next(node);
    }
    rb_erase_augmented(node, &root->rb_root, augment);
}


/*
 * Interval trees.
 *
 * Derived from lib/interval_tree.c and its dependencies,
 * especially include/linux/interval_tree_generic.h.
 */

#define rb_to_itree(N)  container_of(N, IntervalTreeNode, rb)

static bool interval_tree_compute_max(IntervalTreeNode *node, bool exit)
{
    IntervalTreeNode *child;
    uint64_t max = node->last;

    if (node->rb.rb_left) {
        child = rb_to_itree(node->rb.rb_left);
        if (child->subtree_last > max) {
            max = child->subtree_last;
        }
    }
    if (node->rb.rb_right) {
        child = rb_to_itree(node->rb.rb_right);
        if (child->subtree_last > max) {
            max = child->subtree_last;
        }
    }
    if (exit && node->subtree_last == max) {
        return true;
    }
    node->subtree_last = max;
    return false;
}

static void interval_tree_propagate(RBNode *rb, RBNode *stop)
{
    while (rb != stop) {
        IntervalTreeNode *node = rb_to_itree(rb);
        if (interval_tree_compute_max(node, true)) {
            break;
        }
        rb = rb_parent(&node->rb);
    }
}

static void interval_tree_copy(RBNode *rb_old, RBNode *rb_new)
{
    IntervalTreeNode *old = rb_to_itree(rb_old);
    IntervalTreeNode *new = rb_to_itree(rb_new);

    new->subtree_last = old->subtree_last;
}

static void interval_tree_rotate(RBNode *rb_old, RBNode *rb_new)
{
    IntervalTreeNode *old = rb_to_itree(rb_old);
    IntervalTreeNode *new = rb_to_itree(rb_new);

    new->subtree_last = old->subtree_last;
    interval_tree_compute_max(old, false);
}

static const RBAugmentCallbacks interval_tree_augment = {
    .propagate = interval_tree_propagate,
    .copy = interval_tree_copy,
    .rotate = interval_tree_rotate,
};

/* Insert / remove interval nodes from the tree */
void interval_tree_insert(IntervalTreeNode *node, IntervalTreeRoot *root)
{
    RBNode **link = &root->rb_root.rb_node, *rb_parent = NULL;
    uint64_t start = node->start, last = node->last;
    IntervalTreeNode *parent;
    bool leftmost = true;

    while (*link) {
        rb_parent = *link;
        parent = rb_to_itree(rb_parent);

        if (parent->subtree_last < last) {
            parent->subtree_last = last;
        }
        if (start < parent->start) {
            link = &parent->rb.rb_left;
        } else {
            link = &parent->rb.rb_right;
            leftmost = false;
        }
    }

    node->subtree_last = last;
    rb_link_node(&node->rb, rb_parent, link);
    rb_insert_augmented_cached(&node->rb, root, leftmost,
                               &interval_tree_augment);
}

void interval_tree_remove(IntervalTreeNode *node, IntervalTreeRoot *root)
{
    rb_erase_augmented_cached(&node->rb, root, &interval_tree_augment);
}

/*
 * Iterate over intervals intersecting [start;last]
 *
 * Note that a node's interval intersects [start;last] iff:
 *   Cond1: node->start <= last
 * and
 *   Cond2: start <= node->last
 */

static IntervalTreeNode *interval_tree_subtree_search(IntervalTreeNode *node,
                                                      uint64_t start,
                                                      uint64_t last)
{
    while (true) {
        /*
         * Loop invariant: start <= node->subtree_last
         * (Cond2 is satisfied by one of the subtree nodes)
         */
        RBNode *tmp = qatomic_read(&node->rb.rb_left);
        if (tmp) {
            IntervalTreeNode *left = rb_to_itree(tmp);

            if (start <= left->subtree_last) {
                /*
                 * Some nodes in left subtree satisfy Cond2.
                 * Iterate to find the leftmost such node N.
                 * If it also satisfies Cond1, that's the
                 * match we are looking for. Otherwise, there
                 * is no matching interval as nodes to the
                 * right of N can't satisfy Cond1 either.
                 */
                node = left;
                continue;
            }
        }
        if (node->start <= last) {         /* Cond1 */
            if (start <= node->last) {     /* Cond2 */
                return node; /* node is leftmost match */
            }
            tmp = qatomic_read(&node->rb.rb_right);
            if (tmp) {
                node = rb_to_itree(tmp);
                if (start <= node->subtree_last) {
                    continue;
                }
            }
        }
        return NULL; /* no match */
    }
}

IntervalTreeNode *interval_tree_iter_first(IntervalTreeRoot *root,
                                           uint64_t start, uint64_t last)
{
    IntervalTreeNode *node, *leftmost;

    if (!root->rb_root.rb_node) {
        return NULL;
    }

    /*
     * Fastpath range intersection/overlap between A: [a0, a1] and
     * B: [b0, b1] is given by:
     *
     *         a0 <= b1 && b0 <= a1
     *
     *  ... where A holds the lock range and B holds the smallest
     * 'start' and largest 'last' in the tree. For the later, we
     * rely on the root node, which by augmented interval tree
     * property, holds the largest value in its last-in-subtree.
     * This allows mitigating some of the tree walk overhead for
     * for non-intersecting ranges, maintained and consulted in O(1).
     */
    node = rb_to_itree(root->rb_root.rb_node);
    if (node->subtree_last < start) {
        return NULL;
    }

    leftmost = rb_to_itree(root->rb_leftmost);
    if (leftmost->start > last) {
        return NULL;
    }

    return interval_tree_subtree_search(node, start, last);
}

IntervalTreeNode *interval_tree_iter_next(IntervalTreeNode *node,
                                          uint64_t start, uint64_t last)
{
    RBNode *rb, *prev;

    rb = qatomic_read(&node->rb.rb_right);
    while (true) {
        /*
         * Loop invariants:
         *   Cond1: node->start <= last
         *   rb == node->rb.rb_right
         *
         * First, search right subtree if suitable
         */
        if (rb) {
            IntervalTreeNode *right = rb_to_itree(rb);

            if (start <= right->subtree_last) {
                return interval_tree_subtree_search(right, start, last);
            }
        }

        /* Move up the tree until we come from a node's left child */
        do {
            rb = rb_parent(&node->rb);
            if (!rb) {
                return NULL;
            }
            prev = &node->rb;
            node = rb_to_itree(rb);
            rb = qatomic_read(&node->rb.rb_right);
        } while (prev == rb);

        /* Check if the node intersects [start;last] */
        if (last < node->start) {  /* !Cond1 */
            return NULL;
        }
        if (start <= node->last) { /* Cond2 */
            return node;
        }
    }
}

/* Occasionally useful for calling from within the debugger. */
#if 0
static void debug_interval_tree_int(IntervalTreeNode *node,
                                    const char *dir, int level)
{
    printf("%4d %*s %s [%" PRIu64 ",%" PRIu64 "] subtree_last:%" PRIu64 "\n",
           level, level + 1, dir, rb_is_red(&node->rb) ? "r" : "b",
           node->start, node->last, node->subtree_last);

    if (node->rb.rb_left) {
        debug_interval_tree_int(rb_to_itree(node->rb.rb_left), "<", level + 1);
    }
    if (node->rb.rb_right) {
        debug_interval_tree_int(rb_to_itree(node->rb.rb_right), ">", level + 1);
    }
}

void debug_interval_tree(IntervalTreeNode *node);
void debug_interval_tree(IntervalTreeNode *node)
{
    if (node) {
        debug_interval_tree_int(node, "*", 0);
    } else {
        printf("null\n");
    }
}
#endif
