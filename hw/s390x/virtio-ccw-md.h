/*
 * Virtio CCW support for abstract virtio based memory device
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_S390X_VIRTIO_CCW_MD_H
#define HW_S390X_VIRTIO_CCW_MD_H

#include "virtio-ccw.h"
#include "qom/object.h"

/*
 * virtio-md-ccw: This extends VirtioCcwDevice.
 */
#define TYPE_VIRTIO_MD_CCW "virtio-md-ccw"

OBJECT_DECLARE_TYPE(VirtIOMDCcw, VirtIOMDCcwClass, VIRTIO_MD_CCW)

struct VirtIOMDCcwClass {
    /* private */
    VirtIOCCWDeviceClass parent;

    /* public */
    void (*unplug_request_check)(VirtIOMDCcw *vmd, Error **errp);
};

struct VirtIOMDCcw {
    VirtioCcwDevice parent_obj;
};

void virtio_ccw_md_pre_plug(VirtIOMDCcw *vmd, MachineState *ms, Error **errp);
void virtio_ccw_md_plug(VirtIOMDCcw *vmd, MachineState *ms, Error **errp);
void virtio_ccw_md_unplug_request(VirtIOMDCcw *vmd, MachineState *ms,
                                  Error **errp);
void virtio_ccw_md_unplug(VirtIOMDCcw *vmd, MachineState *ms, Error **errp);

#endif /* HW_S390X_VIRTIO_CCW_MD_H */
