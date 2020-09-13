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
#include "qapi/qapi-types-machine.h"
#include "qom/object.h"

#define TYPE_VIRTIO_PMEM "virtio-pmem"

OBJECT_DECLARE_TYPE(VirtIOPMEM, VirtIOPMEMClass,
                    VIRTIO_PMEM)

#define VIRTIO_PMEM_ADDR_PROP "memaddr"
#define VIRTIO_PMEM_MEMDEV_PROP "memdev"

struct VirtIOPMEM {
    VirtIODevice parent_obj;

    VirtQueue *rq_vq;
    uint64_t start;
    HostMemoryBackend *memdev;
};

struct VirtIOPMEMClass {
    /* private */
    VirtIODevice parent;

    /* public */
    void (*fill_device_info)(const VirtIOPMEM *pmem, VirtioPMEMDeviceInfo *vi);
    MemoryRegion *(*get_memory_region)(VirtIOPMEM *pmem, Error **errp);
};

#endif
