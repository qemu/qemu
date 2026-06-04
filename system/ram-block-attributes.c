/*
 * QEMU ram block attributes
 *
 * Copyright Intel
 *
 * Author:
 *      Chenyi Qiang <chenyi.qiang@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/ramblock.h"
#include "trace.h"

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(RamBlockAttributes,
                                          ram_block_attributes,
                                          RAM_BLOCK_ATTRIBUTES,
                                          OBJECT,
                                          { TYPE_RAM_DISCARD_SOURCE },
                                          { })

static size_t
ram_block_attributes_get_block_size(void)
{
    /*
     * Because page conversion could be manipulated in the size of at least 4K
     * or 4K aligned, Use the host page size as the granularity to track the
     * memory attribute.
     */
    return qemu_real_host_page_size();
}

/* RamDiscardSource interface implementation */
static uint64_t
ram_block_attributes_rds_get_min_granularity(const RamDiscardSource *rds,
                                             const MemoryRegion *mr)
{
    const RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rds);

    g_assert(mr == attr->ram_block->mr);
    return ram_block_attributes_get_block_size();
}

static bool
ram_block_attributes_rds_is_populated(const RamDiscardSource *rds,
                                      const MemoryRegionSection *section)
{
    const RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rds);
    const size_t block_size = ram_block_attributes_get_block_size();
    const uint64_t first_bit = section->offset_within_region / block_size;
    const uint64_t last_bit =
        first_bit + int128_get64(section->size) / block_size - 1;
    unsigned long first_discarded_bit;

    first_discarded_bit = find_next_zero_bit(attr->bitmap, last_bit + 1,
                                           first_bit);
    return first_discarded_bit > last_bit;
}

static bool
ram_block_attributes_is_valid_range(RamBlockAttributes *attr, uint64_t offset,
                                    uint64_t size)
{
    MemoryRegion *mr = attr->ram_block->mr;

    g_assert(mr);

    uint64_t region_size = memory_region_size(mr);
    const size_t block_size = ram_block_attributes_get_block_size();

    if (!QEMU_IS_ALIGNED(offset, block_size) ||
        !QEMU_IS_ALIGNED(size, block_size)) {
        return false;
    }
    if (offset + size <= offset) {
        return false;
    }
    if (offset + size > region_size) {
        return false;
    }
    return true;
}

static void
ram_block_attributes_notify_discard(RamBlockAttributes *attr,
                                    uint64_t offset,
                                    uint64_t size)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(attr->ram_block->mr);

    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(attr),
                                       offset, size);
}

static int
ram_block_attributes_notify_populate(RamBlockAttributes *attr,
                                     uint64_t offset, uint64_t size)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(attr->ram_block->mr);

    return ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(attr),
                                               offset, size);
}

int ram_block_attributes_state_change(RamBlockAttributes *attr,
                                      uint64_t offset, uint64_t size,
                                      bool to_discard)
{
    const size_t block_size = ram_block_attributes_get_block_size();
    const unsigned long first_bit = offset / block_size;
    const unsigned long nbits = size / block_size;
    const unsigned long last_bit = first_bit + nbits - 1;
    const bool is_discarded = find_next_bit(attr->bitmap, attr->bitmap_size,
                                            first_bit) > last_bit;
    const bool is_populated = find_next_zero_bit(attr->bitmap,
                                attr->bitmap_size, first_bit) > last_bit;
    unsigned long bit;
    int ret = 0;

    if (!ram_block_attributes_is_valid_range(attr, offset, size)) {
        error_report("%s, invalid range: offset 0x%" PRIx64 ", size "
                     "0x%" PRIx64, __func__, offset, size);
        return -EINVAL;
    }

    trace_ram_block_attributes_state_change(offset, size,
                                            is_discarded ? "discarded" :
                                            is_populated ? "populated" :
                                            "mixture",
                                            to_discard ? "discarded" :
                                            "populated");
    if (to_discard) {
        if (is_discarded) {
            /* Already private */
        } else if (is_populated) {
            /* Completely shared */
            bitmap_clear(attr->bitmap, first_bit, nbits);
            ram_block_attributes_notify_discard(attr, offset, size);
        } else {
            /* Unexpected mixture: process individual blocks */
            for (bit = first_bit; bit < first_bit + nbits; bit++) {
                if (!test_bit(bit, attr->bitmap)) {
                    continue;
                }
                clear_bit(bit, attr->bitmap);
                ram_block_attributes_notify_discard(attr, bit * block_size,
                                                    block_size);
            }
        }
    } else {
        if (is_populated) {
            /* Already shared */
        } else if (is_discarded) {
            /* Completely private */
            bitmap_set(attr->bitmap, first_bit, nbits);
            ret = ram_block_attributes_notify_populate(attr, offset, size);
        } else {
            /* Unexpected mixture: process individual blocks */
            for (bit = first_bit; bit < first_bit + nbits; bit++) {
                if (test_bit(bit, attr->bitmap)) {
                    continue;
                }
                set_bit(bit, attr->bitmap);
                ret = ram_block_attributes_notify_populate(attr,
                                                           bit * block_size,
                                                           block_size);
                if (ret) {
                    break;
                }
            }
        }
    }

    return ret;
}

RamBlockAttributes *ram_block_attributes_create(RAMBlock *ram_block)
{
    const int block_size = ram_block_attributes_get_block_size();
    RamBlockAttributes *attr;
    MemoryRegion *mr = ram_block->mr;

    attr = RAM_BLOCK_ATTRIBUTES(object_new(TYPE_RAM_BLOCK_ATTRIBUTES));

    attr->ram_block = ram_block;

    if (memory_region_add_ram_discard_source(mr, RAM_DISCARD_SOURCE(attr))) {
        object_unref(OBJECT(attr));
        return NULL;
    }
    attr->bitmap_size = DIV_ROUND_UP(int128_get64(mr->size), block_size);
    attr->bitmap = bitmap_new(attr->bitmap_size);

    return attr;
}

void ram_block_attributes_destroy(RamBlockAttributes *attr)
{
    g_assert(attr);

    g_free(attr->bitmap);
    memory_region_del_ram_discard_source(attr->ram_block->mr, RAM_DISCARD_SOURCE(attr));
    object_unref(OBJECT(attr));
}

static void ram_block_attributes_init(Object *obj)
{
}

static void ram_block_attributes_finalize(Object *obj)
{
}

static void ram_block_attributes_class_init(ObjectClass *klass,
                                            const void *data)
{
    RamDiscardSourceClass *rdsc = RAM_DISCARD_SOURCE_CLASS(klass);

    rdsc->get_min_granularity = ram_block_attributes_rds_get_min_granularity;
    rdsc->is_populated = ram_block_attributes_rds_is_populated;
}
