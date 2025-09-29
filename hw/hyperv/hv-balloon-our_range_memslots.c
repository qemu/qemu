/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/ramblock.h"
#include "hv-balloon-internal.h"
#include "hv-balloon-our_range_memslots.h"
#include "trace.h"

/* OurRange */
static void our_range_init(OurRange *our_range, uint64_t start, uint64_t count)
{
    assert(count <= UINT64_MAX - start);
    our_range->range.start = start;
    our_range->range.count = count;

    hvb_page_range_tree_init(&our_range->removed_guest);
    hvb_page_range_tree_init(&our_range->removed_both);

    /* mark the whole range as unused but for potential use */
    our_range->added = 0;
    our_range->unusable_tail = 0;
}

static void our_range_destroy(OurRange *our_range)
{
    hvb_page_range_tree_destroy(&our_range->removed_guest);
    hvb_page_range_tree_destroy(&our_range->removed_both);
}

void hvb_our_range_clear_removed_trees(OurRange *our_range)
{
    hvb_page_range_tree_destroy(&our_range->removed_guest);
    hvb_page_range_tree_destroy(&our_range->removed_both);
    hvb_page_range_tree_init(&our_range->removed_guest);
    hvb_page_range_tree_init(&our_range->removed_both);
}

void hvb_our_range_mark_added(OurRange *our_range, uint64_t additional_size)
{
    assert(additional_size <= UINT64_MAX - our_range->added);

    our_range->added += additional_size;

    assert(our_range->added <= UINT64_MAX - our_range->unusable_tail);
    assert(our_range->added + our_range->unusable_tail <=
           our_range->range.count);
}

/* OurRangeMemslots */
static void our_range_memslots_init_slots(OurRangeMemslots *our_range,
                                          MemoryRegion *backing_mr,
                                          Object *memslot_owner)
{
    OurRangeMemslotsSlots *memslots = &our_range->slots;
    unsigned int idx;
    uint64_t memslot_offset;

    assert(memslots->count > 0);
    memslots->slots = g_new0(MemoryRegion, memslots->count);

    /* Initialize our memslots, but don't map them yet. */
    assert(memslots->size_each > 0);
    for (idx = 0, memslot_offset = 0; idx < memslots->count;
         idx++, memslot_offset += memslots->size_each) {
        uint64_t memslot_size;
        g_autofree char *name = NULL;

        /* The size of the last memslot might be smaller. */
        if (idx == memslots->count - 1) {
            uint64_t region_size;

            assert(our_range->mr);
            region_size = memory_region_size(our_range->mr);
            memslot_size = region_size - memslot_offset;
        } else {
            memslot_size = memslots->size_each;
        }

        name = g_strdup_printf("memslot-%u", idx);
        memory_region_init_alias(&memslots->slots[idx], memslot_owner, name,
                                 backing_mr, memslot_offset, memslot_size);
        /*
         * We want to be able to atomically and efficiently activate/deactivate
         * individual memslots without affecting adjacent memslots in memory
         * notifiers.
         */
        memory_region_set_unmergeable(&memslots->slots[idx], true);
    }

    memslots->mapped_count = 0;
}

OurRangeMemslots *hvb_our_range_memslots_new(uint64_t addr,
                                             MemoryRegion *parent_mr,
                                             MemoryRegion *backing_mr,
                                             Object *memslot_owner,
                                             unsigned int memslot_count,
                                             uint64_t memslot_size)
{
    OurRangeMemslots *our_range;

    our_range = g_malloc(sizeof(*our_range));
    our_range_init(&our_range->range,
                   addr / HV_BALLOON_PAGE_SIZE,
                   memory_region_size(parent_mr) / HV_BALLOON_PAGE_SIZE);
    our_range->slots.size_each = memslot_size;
    our_range->slots.count = memslot_count;
    our_range->mr = parent_mr;
    our_range_memslots_init_slots(our_range, backing_mr, memslot_owner);

    return our_range;
}

static void our_range_memslots_free_memslots(OurRangeMemslots *our_range)
{
    OurRangeMemslotsSlots *memslots = &our_range->slots;
    unsigned int idx;
    uint64_t offset;

    memory_region_transaction_begin();
    for (idx = 0, offset = 0; idx < memslots->mapped_count;
         idx++, offset += memslots->size_each) {
        trace_hv_balloon_unmap_slot(idx, memslots->count, offset);
        assert(memory_region_is_mapped(&memslots->slots[idx]));
        memory_region_del_subregion(our_range->mr, &memslots->slots[idx]);
    }
    memory_region_transaction_commit();

    for (idx = 0; idx < memslots->count; idx++) {
        object_unparent(OBJECT(&memslots->slots[idx]));
    }

    g_clear_pointer(&our_range->slots.slots, g_free);
}

void hvb_our_range_memslots_free(OurRangeMemslots *our_range)
{
    OurRangeMemslotsSlots *memslots = &our_range->slots;
    MemoryRegion *hostmem_mr;
    RAMBlock *rb;

    assert(our_range->slots.count > 0);
    assert(our_range->slots.slots);

    hostmem_mr = memslots->slots[0].alias;
    rb = hostmem_mr->ram_block;
    ram_block_discard_range(rb, 0, qemu_ram_get_used_length(rb));

    our_range_memslots_free_memslots(our_range);
    our_range_destroy(&our_range->range);
    g_free(our_range);
}

void hvb_our_range_memslots_ensure_mapped_additional(OurRangeMemslots *our_range,
                                                     uint64_t additional_map_size)
{
    OurRangeMemslotsSlots *memslots = &our_range->slots;
    uint64_t total_map_size;
    unsigned int idx;
    uint64_t offset;

    total_map_size = (our_range->range.added + additional_map_size) *
        HV_BALLOON_PAGE_SIZE;
    idx = memslots->mapped_count;
    assert(memslots->size_each > 0);
    offset = idx * memslots->size_each;

    /*
     * Activate all memslots covered by the newly added region in a single
     * transaction.
     */
    memory_region_transaction_begin();
    for ( ; idx < memslots->count;
          idx++, offset += memslots->size_each) {
        /*
         * If this memslot starts beyond or at the end of the range to map so
         * does every next one.
         */
        if (offset >= total_map_size) {
            break;
        }

        /*
         * Instead of enabling/disabling memslot, we add/remove them. This
         * should make address space updates faster, because we don't have to
         * loop over many disabled subregions.
         */
        trace_hv_balloon_map_slot(idx, memslots->count, offset);
        assert(!memory_region_is_mapped(&memslots->slots[idx]));
        memory_region_add_subregion(our_range->mr, offset,
                                    &memslots->slots[idx]);

        memslots->mapped_count++;
    }
    memory_region_transaction_commit();
}
