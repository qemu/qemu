/*
 * Virtio PMEM device
 *
 * Copyright (C) 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Pankaj Gupta <pagupta@redhat.com>
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_PMEM_H
#define HW_VIRTIO_PMEM_H

#include "hw/virtio/virtio.h"
#include "sysemu/hostmem.h"

#define TYPE_VIRTIO_PMEM "virtio-pmem"

#define VIRTIO_PMEM(obj) \
        OBJECT_CHECK(VirtIOPMEM, (obj), TYPE_VIRTIO_PMEM)
#define VIRTIO_PMEM_CLASS(oc) \
        OBJECT_CLASS_CHECK(VirtIOPMEMClass, (oc), TYPE_VIRTIO_PMEM)
#define VIRTIO_PMEM_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtIOPMEMClass, (obj), TYPE_VIRTIO_PMEM)

#define VIRTIO_PMEM_ADDR_PROP "memaddr"
#define VIRTIO_PMEM_MEMDEV_PROP "memdev"

typedef struct VirtIOPMEM {
    VirtIODevice parent_obj;

    VirtQueue *rq_vq;
    uint64_t start;
    HostMemoryBackend *memdev;
} VirtIOPMEM;

typedef struct VirtIOPMEMClass {
    /* private */
    VirtIODevice parent;

    /* public */
    void (*fill_device_info)(const VirtIOPMEM *pmem, VirtioPMEMDeviceInfo *vi);
    MemoryRegion *(*get_memory_region)(VirtIOPMEM *pmem, Error **errp);
} VirtIOPMEMClass;

#endif
