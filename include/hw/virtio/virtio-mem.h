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
#include "hw/virtio/virtio.h"
#include "qapi/qapi-types-misc.h"
#include "sysemu/hostmem.h"
#include "qom/object.h"

#define TYPE_VIRTIO_MEM "virtio-mem"

OBJECT_DECLARE_TYPE(VirtIOMEM, VirtIOMEMClass,
                    VIRTIO_MEM)

#define VIRTIO_MEM_MEMDEV_PROP "memdev"
#define VIRTIO_MEM_NODE_PROP "node"
#define VIRTIO_MEM_SIZE_PROP "size"
#define VIRTIO_MEM_REQUESTED_SIZE_PROP "requested-size"
#define VIRTIO_MEM_BLOCK_SIZE_PROP "block-size"
#define VIRTIO_MEM_ADDR_PROP "memaddr"
#define VIRTIO_MEM_UNPLUGGED_INACCESSIBLE_PROP "unplugged-inaccessible"
#define VIRTIO_MEM_PREALLOC_PROP "prealloc"

struct VirtIOMEM {
    VirtIODevice parent_obj;

    /* guest -> host request queue */
    VirtQueue *vq;

    /* bitmap used to track unplugged memory */
    int32_t bitmap_size;
    unsigned long *bitmap;

    /* assigned memory backend and memory region */
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

    /* notifiers to notify when "size" changes */
    NotifierList size_change_notifiers;

    /* listeners to notify on plug/unplug activity. */
    QLIST_HEAD(, RamDiscardListener) rdl_list;
};

struct VirtIOMEMClass {
    /* private */
    VirtIODevice parent;

    /* public */
    void (*fill_device_info)(const VirtIOMEM *vmen, VirtioMEMDeviceInfo *vi);
    MemoryRegion *(*get_memory_region)(VirtIOMEM *vmem, Error **errp);
    void (*add_size_change_notifier)(VirtIOMEM *vmem, Notifier *notifier);
    void (*remove_size_change_notifier)(VirtIOMEM *vmem, Notifier *notifier);
};

#endif
