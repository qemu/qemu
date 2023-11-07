/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HV_BALLOON_OUR_RANGE_MEMSLOTS_H
#define HW_HYPERV_HV_BALLOON_OUR_RANGE_MEMSLOTS_H

#include "qemu/osdep.h"

#include "exec/memory.h"
#include "qom/object.h"
#include "hv-balloon-page_range_tree.h"

/* OurRange */
#define OUR_RANGE(ptr) ((OurRange *)(ptr))

/* "our range" means the memory range owned by this driver (for hot-adding) */
typedef struct OurRange {
    PageRange range;

    /* How many pages were hot-added to the guest */
    uint64_t added;

    /* Pages at the end not currently usable */
    uint64_t unusable_tail;

    /* Memory removed from the guest */
    PageRangeTree removed_guest, removed_both;
} OurRange;

static inline uint64_t our_range_get_remaining_start(OurRange *our_range)
{
    return our_range->range.start + our_range->added;
}

static inline uint64_t our_range_get_remaining_size(OurRange *our_range)
{
    return our_range->range.count - our_range->added - our_range->unusable_tail;
}

void hvb_our_range_mark_added(OurRange *our_range, uint64_t additional_size);

static inline void our_range_mark_remaining_unusable(OurRange *our_range)
{
    our_range->unusable_tail = our_range->range.count - our_range->added;
}

static inline PageRangeTree our_range_get_removed_tree(OurRange *our_range,
                                                       bool both)
{
    if (both) {
        return our_range->removed_both;
    } else {
        return our_range->removed_guest;
    }
}

static inline bool our_range_is_removed_tree_empty(OurRange *our_range,
                                                   bool both)
{
    if (both) {
        return page_range_tree_is_empty(our_range->removed_both);
    } else {
        return page_range_tree_is_empty(our_range->removed_guest);
    }
}

void hvb_our_range_clear_removed_trees(OurRange *our_range);

/* OurRangeMemslots */
typedef struct OurRangeMemslotsSlots {
    /* Nominal size of each memslot (the last one might be smaller) */
    uint64_t size_each;

    /* Slots array and its element count */
    MemoryRegion *slots;
    unsigned int count;

    /* How many slots are currently mapped */
    unsigned int mapped_count;
} OurRangeMemslotsSlots;

typedef struct OurRangeMemslots {
    OurRange range;

    /* Memslots covering our range */
    OurRangeMemslotsSlots slots;

    MemoryRegion *mr;
} OurRangeMemslots;

OurRangeMemslots *hvb_our_range_memslots_new(uint64_t addr,
                                             MemoryRegion *parent_mr,
                                             MemoryRegion *backing_mr,
                                             Object *memslot_owner,
                                             unsigned int memslot_count,
                                             uint64_t memslot_size);
void hvb_our_range_memslots_free(OurRangeMemslots *our_range);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(OurRangeMemslots, hvb_our_range_memslots_free)

void hvb_our_range_memslots_ensure_mapped_additional(OurRangeMemslots *our_range,
                                                     uint64_t additional_map_size);

#endif
