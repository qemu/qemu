/*
 * Virtio MEM CCW device
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_S390X_VIRTIO_CCW_MEM_H
#define HW_S390X_VIRTIO_CCW_MEM_H

#include "virtio-ccw-md.h"
#include "hw/virtio/virtio-mem.h"
#include "qom/object.h"

typedef struct VirtIOMEMCcw VirtIOMEMCcw;

/*
 * virtio-mem-ccw: This extends VirtIOMDCcw
 */
#define TYPE_VIRTIO_MEM_CCW "virtio-mem-ccw"
DECLARE_INSTANCE_CHECKER(VirtIOMEMCcw, VIRTIO_MEM_CCW, TYPE_VIRTIO_MEM_CCW)

struct VirtIOMEMCcw {
    VirtIOMDCcw parent_obj;
    VirtIOMEM vdev;
    Notifier size_change_notifier;
};

#endif /* HW_S390X_VIRTIO_CCW_MEM_H */
