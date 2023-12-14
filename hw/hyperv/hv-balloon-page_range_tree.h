/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HV_BALLOON_PAGE_RANGE_TREE_H
#define HW_HYPERV_HV_BALLOON_PAGE_RANGE_TREE_H

#include "qemu/osdep.h"

/* PageRange */
typedef struct PageRange {
    uint64_t start;
    uint64_t count;
} PageRange;

/* return just the part of range before (start) */
static inline void page_range_part_before(const PageRange *range,
                                          uint64_t start, PageRange *out)
{
    uint64_t endr = range->start + range->count;
    uint64_t end = MIN(endr, start);

    out->start = range->start;
    if (end > out->start) {
        out->count = end - out->start;
    } else {
        out->count = 0;
    }
}

/* return just the part of range after (start, count) */
static inline void page_range_part_after(const PageRange *range,
                                         uint64_t start, uint64_t count,
                                         PageRange *out)
{
    uint64_t end = range->start + range->count;
    uint64_t ends = start + count;

    out->start = MAX(range->start, ends);
    if (end > out->start) {
        out->count = end - out->start;
    } else {
        out->count = 0;
    }
}

static inline void page_range_intersect(const PageRange *range,
                                        uint64_t start, uint64_t count,
                                        PageRange *out)
{
    uint64_t end1 = range->start + range->count;
    uint64_t end2 = start + count;
    uint64_t end = MIN(end1, end2);

    out->start = MAX(range->start, start);
    out->count = out->start < end ? end - out->start : 0;
}

static inline uint64_t page_range_intersection_size(const PageRange *range,
                                                    uint64_t start, uint64_t count)
{
    PageRange trange;

    page_range_intersect(range, start, count, &trange);
    return trange.count;
}

static inline bool page_range_joinable_left(const PageRange *range,
                                            uint64_t start, uint64_t count)
{
    return start + count == range->start;
}

static inline bool page_range_joinable_right(const PageRange *range,
                                             uint64_t start, uint64_t count)
{
    return range->start + range->count == start;
}

static inline bool page_range_joinable(const PageRange *range,
                                       uint64_t start, uint64_t count)
{
    return page_range_joinable_left(range, start, count) ||
        page_range_joinable_right(range, start, count);
}

/* PageRangeTree */
/* type safety */
typedef struct PageRangeTree {
    GTree *t;
} PageRangeTree;

static inline bool page_range_tree_is_empty(PageRangeTree tree)
{
    guint nnodes = g_tree_nnodes(tree.t);

    return nnodes == 0;
}

void hvb_page_range_tree_init(PageRangeTree *tree);
void hvb_page_range_tree_destroy(PageRangeTree *tree);

bool hvb_page_range_tree_intree_any(PageRangeTree tree,
                                    uint64_t start, uint64_t count);

bool hvb_page_range_tree_pop(PageRangeTree tree, PageRange *out,
                             uint64_t maxcount);

void hvb_page_range_tree_insert(PageRangeTree tree,
                                uint64_t start, uint64_t count,
                                uint64_t *dupcount);

#endif
