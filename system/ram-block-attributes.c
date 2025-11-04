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
                                          { TYPE_RAM_DISCARD_MANAGER },
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


static bool
ram_block_attributes_rdm_is_populated(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section)
{
    const RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rdm);
    const size_t block_size = ram_block_attributes_get_block_size();
    const uint64_t first_bit = section->offset_within_region / block_size;
    const uint64_t last_bit =
        first_bit + int128_get64(section->size) / block_size - 1;
    unsigned long first_discarded_bit;

    first_discarded_bit = find_next_zero_bit(attr->bitmap, last_bit + 1,
                                           first_bit);
    return first_discarded_bit > last_bit;
}

typedef int (*ram_block_attributes_section_cb)(MemoryRegionSection *s,
                                               void *arg);

static int
ram_block_attributes_notify_populate_cb(MemoryRegionSection *section,
                                        void *arg)
{
    RamDiscardListener *rdl = arg;

    return rdl->notify_populate(rdl, section);
}

static int
ram_block_attributes_notify_discard_cb(MemoryRegionSection *section,
                                       void *arg)
{
    RamDiscardListener *rdl = arg;

    rdl->notify_discard(rdl, section);
    return 0;
}

static int
ram_block_attributes_for_each_populated_section(const RamBlockAttributes *attr,
                                                MemoryRegionSection *section,
                                                void *arg,
                                                ram_block_attributes_section_cb cb)
{
    unsigned long first_bit, last_bit;
    uint64_t offset, size;
    const size_t block_size = ram_block_attributes_get_block_size();
    int ret = 0;

    first_bit = section->offset_within_region / block_size;
    first_bit = find_next_bit(attr->bitmap, attr->bitmap_size,
                              first_bit);

    while (first_bit < attr->bitmap_size) {
        MemoryRegionSection tmp = *section;

        offset = first_bit * block_size;
        last_bit = find_next_zero_bit(attr->bitmap, attr->bitmap_size,
                                      first_bit + 1) - 1;
        size = (last_bit - first_bit + 1) * block_size;

        if (!memory_region_section_intersect_range(&tmp, offset, size)) {
            break;
        }

        ret = cb(&tmp, arg);
        if (ret) {
            error_report("%s: Failed to notify RAM discard listener: %s",
                         __func__, strerror(-ret));
            break;
        }

        first_bit = find_next_bit(attr->bitmap, attr->bitmap_size,
                                  last_bit + 2);
    }

    return ret;
}

static int
ram_block_attributes_for_each_discarded_section(const RamBlockAttributes *attr,
                                                MemoryRegionSection *section,
                                                void *arg,
                                                ram_block_attributes_section_cb cb)
{
    unsigned long first_bit, last_bit;
    uint64_t offset, size;
    const size_t block_size = ram_block_attributes_get_block_size();
    int ret = 0;

    first_bit = section->offset_within_region / block_size;
    first_bit = find_next_zero_bit(attr->bitmap, attr->bitmap_size,
                                   first_bit);

    while (first_bit < attr->bitmap_size) {
        MemoryRegionSection tmp = *section;

        offset = first_bit * block_size;
        last_bit = find_next_bit(attr->bitmap, attr->bitmap_size,
                                 first_bit + 1) - 1;
        size = (last_bit - first_bit + 1) * block_size;

        if (!memory_region_section_intersect_range(&tmp, offset, size)) {
            break;
        }

        ret = cb(&tmp, arg);
        if (ret) {
            error_report("%s: Failed to notify RAM discard listener: %s",
                         __func__, strerror(-ret));
            break;
        }

        first_bit = find_next_zero_bit(attr->bitmap,
                                       attr->bitmap_size,
                                       last_bit + 2);
    }

    return ret;
}

static uint64_t
ram_block_attributes_rdm_get_min_granularity(const RamDiscardManager *rdm,
                                             const MemoryRegion *mr)
{
    const RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rdm);

    g_assert(mr == attr->ram_block->mr);
    return ram_block_attributes_get_block_size();
}

static void
ram_block_attributes_rdm_register_listener(RamDiscardManager *rdm,
                                           RamDiscardListener *rdl,
                                           MemoryRegionSection *section)
{
    RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rdm);
    int ret;

    g_assert(section->mr == attr->ram_block->mr);
    rdl->section = memory_region_section_new_copy(section);

    QLIST_INSERT_HEAD(&attr->rdl_list, rdl, next);

    ret = ram_block_attributes_for_each_populated_section(attr, section, rdl,
                                    ram_block_attributes_notify_populate_cb);
    if (ret) {
        error_report("%s: Failed to register RAM discard listener: %s",
                     __func__, strerror(-ret));
        exit(1);
    }
}

static void
ram_block_attributes_rdm_unregister_listener(RamDiscardManager *rdm,
                                             RamDiscardListener *rdl)
{
    RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rdm);
    int ret;

    g_assert(rdl->section);
    g_assert(rdl->section->mr == attr->ram_block->mr);

    if (rdl->double_discard_supported) {
        rdl->notify_discard(rdl, rdl->section);
    } else {
        ret = ram_block_attributes_for_each_populated_section(attr,
                rdl->section, rdl, ram_block_attributes_notify_discard_cb);
        if (ret) {
            error_report("%s: Failed to unregister RAM discard listener: %s",
                         __func__, strerror(-ret));
            exit(1);
        }
    }

    memory_region_section_free_copy(rdl->section);
    rdl->section = NULL;
    QLIST_REMOVE(rdl, next);
}

typedef struct RamBlockAttributesReplayData {
    ReplayRamDiscardState fn;
    void *opaque;
} RamBlockAttributesReplayData;

static int ram_block_attributes_rdm_replay_cb(MemoryRegionSection *section,
                                              void *arg)
{
    RamBlockAttributesReplayData *data = arg;

    return data->fn(section, data->opaque);
}

static int
ram_block_attributes_rdm_replay_populated(const RamDiscardManager *rdm,
                                          MemoryRegionSection *section,
                                          ReplayRamDiscardState replay_fn,
                                          void *opaque)
{
    RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rdm);
    RamBlockAttributesReplayData data = { .fn = replay_fn, .opaque = opaque };

    g_assert(section->mr == attr->ram_block->mr);
    return ram_block_attributes_for_each_populated_section(attr, section, &data,
                                            ram_block_attributes_rdm_replay_cb);
}

static int
ram_block_attributes_rdm_replay_discarded(const RamDiscardManager *rdm,
                                          MemoryRegionSection *section,
                                          ReplayRamDiscardState replay_fn,
                                          void *opaque)
{
    RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(rdm);
    RamBlockAttributesReplayData data = { .fn = replay_fn, .opaque = opaque };

    g_assert(section->mr == attr->ram_block->mr);
    return ram_block_attributes_for_each_discarded_section(attr, section, &data,
                                            ram_block_attributes_rdm_replay_cb);
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

static void ram_block_attributes_notify_discard(RamBlockAttributes *attr,
                                                uint64_t offset,
                                                uint64_t size)
{
    RamDiscardListener *rdl;

    QLIST_FOREACH(rdl, &attr->rdl_list, next) {
        MemoryRegionSection tmp = *rdl->section;

        if (!memory_region_section_intersect_range(&tmp, offset, size)) {
            continue;
        }
        rdl->notify_discard(rdl, &tmp);
    }
}

static int
ram_block_attributes_notify_populate(RamBlockAttributes *attr,
                                     uint64_t offset, uint64_t size)
{
    RamDiscardListener *rdl;
    int ret = 0;

    QLIST_FOREACH(rdl, &attr->rdl_list, next) {
        MemoryRegionSection tmp = *rdl->section;

        if (!memory_region_section_intersect_range(&tmp, offset, size)) {
            continue;
        }
        ret = rdl->notify_populate(rdl, &tmp);
        if (ret) {
            break;
        }
    }

    return ret;
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
    if (memory_region_set_ram_discard_manager(mr, RAM_DISCARD_MANAGER(attr))) {
        object_unref(OBJECT(attr));
        return NULL;
    }
    attr->bitmap_size =
        ROUND_UP(int128_get64(mr->size), block_size) / block_size;
    attr->bitmap = bitmap_new(attr->bitmap_size);

    return attr;
}

void ram_block_attributes_destroy(RamBlockAttributes *attr)
{
    g_assert(attr);

    g_free(attr->bitmap);
    memory_region_set_ram_discard_manager(attr->ram_block->mr, NULL);
    object_unref(OBJECT(attr));
}

static void ram_block_attributes_init(Object *obj)
{
    RamBlockAttributes *attr = RAM_BLOCK_ATTRIBUTES(obj);

    QLIST_INIT(&attr->rdl_list);
}

static void ram_block_attributes_finalize(Object *obj)
{
}

static void ram_block_attributes_class_init(ObjectClass *klass,
                                            const void *data)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_CLASS(klass);

    rdmc->get_min_granularity = ram_block_attributes_rdm_get_min_granularity;
    rdmc->register_listener = ram_block_attributes_rdm_register_listener;
    rdmc->unregister_listener = ram_block_attributes_rdm_unregister_listener;
    rdmc->is_populated = ram_block_attributes_rdm_is_populated;
    rdmc->replay_populated = ram_block_attributes_rdm_replay_populated;
    rdmc->replay_discarded = ram_block_attributes_rdm_replay_discarded;
}
