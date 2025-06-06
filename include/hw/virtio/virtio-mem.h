/*
 * Virtio MEM device
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_MEM_H
#define HW_VIRTIO_MEM_H

#include "standard-headers/linux/virtio_mem.h"
#include "hw/resettable.h"
#include "hw/virtio/virtio.h"
#include "qapi/qapi-types-misc.h"
#include "system/hostmem.h"
#include "qom/object.h"

#define TYPE_VIRTIO_MEM "virtio-mem"

OBJECT_DECLARE_TYPE(VirtIOMEM, VirtIOMEMClass,
                    VIRTIO_MEM)

#define TYPE_VIRTIO_MEM_SYSTEM_RESET "virtio-mem-system-reset"

OBJECT_DECLARE_SIMPLE_TYPE(VirtioMemSystemReset, VIRTIO_MEM_SYSTEM_RESET)

#define VIRTIO_MEM_MEMDEV_PROP "memdev"
#define VIRTIO_MEM_NODE_PROP "node"
#define VIRTIO_MEM_SIZE_PROP "size"
#define VIRTIO_MEM_REQUESTED_SIZE_PROP "requested-size"
#define VIRTIO_MEM_BLOCK_SIZE_PROP "block-size"
#define VIRTIO_MEM_ADDR_PROP "memaddr"
#define VIRTIO_MEM_UNPLUGGED_INACCESSIBLE_PROP "unplugged-inaccessible"
#define VIRTIO_MEM_EARLY_MIGRATION_PROP "x-early-migration"
#define VIRTIO_MEM_PREALLOC_PROP "prealloc"
#define VIRTIO_MEM_DYNAMIC_MEMSLOTS_PROP "dynamic-memslots"

struct VirtIOMEM {
    VirtIODevice parent_obj;

    /* guest -> host request queue */
    VirtQueue *vq;

    /* bitmap used to track unplugged memory */
    int32_t bitmap_size;
    unsigned long *bitmap;

    /*
     * With "dynamic-memslots=on": Device memory region in which we dynamically
     * map the memslots.
     */
    MemoryRegion *mr;

    /*
     * With "dynamic-memslots=on": The individual memslots (aliases into the
     * memory backend).
     */
    MemoryRegion *memslots;

    /* With "dynamic-memslots=on": The total number of memslots. */
    uint16_t nb_memslots;

    /*
     * With "dynamic-memslots=on": Size of one memslot (the size of the
     * last one can differ).
     */
    uint64_t memslot_size;

    /* Assigned memory backend with the RAM memory region. */
    HostMemoryBackend *memdev;

    /* NUMA node */
    uint32_t node;

    /* assigned address of the region in guest physical memory */
    uint64_t addr;

    /* usable region size (<= region_size) */
    uint64_t usable_region_size;

    /* actual size (how much the guest plugged) */
    uint64_t size;

    /* requested size */
    uint64_t requested_size;

    /* block size and alignment */
    uint64_t block_size;

    /*
     * Whether we indicate VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE to the guest.
     * For !x86 targets this will always be "on" and consequently indicate
     * VIRTIO_MEM_F_UNPLUGGED_INACCESSIBLE.
     */
    OnOffAuto unplugged_inaccessible;

    /* whether to prealloc memory when plugging new blocks */
    bool prealloc;

    /*
     * Whether we migrate properties that are immutable while migration is
     * active early, before state of other devices and especially, before
     * migrating any RAM content.
     */
    bool early_migration;

    /*
     * Whether we dynamically map (multiple, if possible) memslots instead of
     * statically mapping the whole RAM memory region.
     */
    bool dynamic_memslots;

    /* notifiers to notify when "size" changes */
    NotifierList size_change_notifiers;

    /* listeners to notify on plug/unplug activity. */
    QLIST_HEAD(, RamDiscardListener) rdl_list;

    /* Catch system resets -> qemu_devices_reset() only. */
    VirtioMemSystemReset *system_reset;
};

struct VirtioMemSystemReset {
    Object parent;

    ResettableState reset_state;
    VirtIOMEM *vmem;
};

struct VirtIOMEMClass {
    /* private */
    VirtioDeviceClass parent_class;

    /* public */
    void (*fill_device_info)(const VirtIOMEM *vmen, VirtioMEMDeviceInfo *vi);
    MemoryRegion *(*get_memory_region)(VirtIOMEM *vmem, Error **errp);
    void (*decide_memslots)(VirtIOMEM *vmem, unsigned int limit);
    unsigned int (*get_memslots)(VirtIOMEM *vmem);
    void (*add_size_change_notifier)(VirtIOMEM *vmem, Notifier *notifier);
    void (*remove_size_change_notifier)(VirtIOMEM *vmem, Notifier *notifier);
    void (*unplug_request_check)(VirtIOMEM *vmem, Error **errp);
};

#endif
