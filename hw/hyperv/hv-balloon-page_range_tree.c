/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hv-balloon-internal.h"
#include "hv-balloon-page_range_tree.h"

/*
 * temporarily avoid warnings about enhanced GTree API usage requiring a
 * too recent Glib version until GLIB_VERSION_MAX_ALLOWED finally reaches
 * the Glib version with this API
 */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

/* PageRangeTree */
static gint page_range_tree_key_compare(gconstpointer leftp,
                                        gconstpointer rightp,
                                        gpointer user_data)
{
    const uint64_t *left = leftp, *right = rightp;

    if (*left < *right) {
        return -1;
    } else if (*left > *right) {
        return 1;
    } else { /* *left == *right */
        return 0;
    }
}

static GTreeNode *page_range_tree_insert_new(PageRangeTree tree,
                                             uint64_t start, uint64_t count)
{
    uint64_t *key = g_malloc(sizeof(*key));
    PageRange *range = g_malloc(sizeof(*range));

    assert(count > 0);

    *key = range->start = start;
    range->count = count;

    return g_tree_insert_node(tree.t, key, range);
}

void hvb_page_range_tree_insert(PageRangeTree tree,
                                uint64_t start, uint64_t count,
                                uint64_t *dupcount)
{
    GTreeNode *node;
    bool joinable;
    uint64_t intersection;
    PageRange *range;

    assert(!SUM_OVERFLOW_U64(start, count));
    if (count == 0) {
        return;
    }

    node = g_tree_upper_bound(tree.t, &start);
    if (node) {
        node = g_tree_node_previous(node);
    } else {
        node = g_tree_node_last(tree.t);
    }

    if (node) {
        range = g_tree_node_value(node);
        assert(range);
        intersection = page_range_intersection_size(range, start, count);
        joinable = page_range_joinable_right(range, start, count);
    }

    if (!node ||
        (!intersection && !joinable)) {
        /*
         * !node case: the tree is empty or the very first node in the tree
         * already has a higher key (the start of its range).
         * the other case: there is a gap in the tree between the new range
         * and the previous one.
         * anyway, let's just insert the new range into the tree.
         */
        node = page_range_tree_insert_new(tree, start, count);
        assert(node);
        range = g_tree_node_value(node);
        assert(range);
    } else {
        /*
         * the previous range in the tree either partially covers the new
         * range or ends just at its beginning - extend it
         */
        if (dupcount) {
            *dupcount += intersection;
        }

        count += start - range->start;
        range->count = MAX(range->count, count);
    }

    /* check next nodes for possible merging */
    for (node = g_tree_node_next(node); node; ) {
        PageRange *rangecur;

        rangecur = g_tree_node_value(node);
        assert(rangecur);

        intersection = page_range_intersection_size(rangecur,
                                                    range->start, range->count);
        joinable = page_range_joinable_left(rangecur,
                                            range->start, range->count);
        if (!intersection && !joinable) {
            /* the current node is disjoint */
            break;
        }

        if (dupcount) {
            *dupcount += intersection;
        }

        count = rangecur->count + (rangecur->start - range->start);
        range->count = MAX(range->count, count);

        /* the current node was merged in, remove it */
        start = rangecur->start;
        node = g_tree_node_next(node);
        /* no hinted removal in GTree... */
        g_tree_remove(tree.t, &start);
    }
}

bool hvb_page_range_tree_pop(PageRangeTree tree, PageRange *out,
                             uint64_t maxcount)
{
    GTreeNode *node;
    PageRange *range;

    node = g_tree_node_last(tree.t);
    if (!node) {
        return false;
    }

    range = g_tree_node_value(node);
    assert(range);

    out->start = range->start;

    /* can't modify range->start as it is the node key */
    if (range->count > maxcount) {
        out->start += range->count - maxcount;
        out->count = maxcount;
        range->count -= maxcount;
    } else {
        out->count = range->count;
        /* no hinted removal in GTree... */
        g_tree_remove(tree.t, &out->start);
    }

    return true;
}

bool hvb_page_range_tree_intree_any(PageRangeTree tree,
                                    uint64_t start, uint64_t count)
{
    GTreeNode *node;

    if (count == 0) {
        return false;
    }

    /* find the first node that can possibly intersect our range */
    node = g_tree_upper_bound(tree.t, &start);
    if (node) {
        /*
         * a NULL node below means that the very first node in the tree
         * already has a higher key (the start of its range).
         */
        node = g_tree_node_previous(node);
    } else {
        /* a NULL node below means that the tree is empty */
        node = g_tree_node_last(tree.t);
    }
    /* node range start <= range start */

    if (!node) {
        /* node range start > range start */
        node = g_tree_node_first(tree.t);
    }

    for ( ; node; node = g_tree_node_next(node)) {
        PageRange *range = g_tree_node_value(node);

        assert(range);
        /*
         * if this node starts beyond or at the end of our range so does
         * every next one
         */
        if (range->start >= start + count) {
            break;
        }

        if (page_range_intersection_size(range, start, count) > 0) {
            return true;
        }
    }

    return false;
}

void hvb_page_range_tree_init(PageRangeTree *tree)
{
    tree->t = g_tree_new_full(page_range_tree_key_compare, NULL,
                              g_free, g_free);
}

void hvb_page_range_tree_destroy(PageRangeTree *tree)
{
    /* g_tree_destroy() is not NULL-safe */
    if (!tree->t) {
        return;
    }

    g_tree_destroy(tree->t);
    tree->t = NULL;
}
