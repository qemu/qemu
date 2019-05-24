/*
 * Virtio GPU PCI Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_GPU_PCI_H
#define HW_VIRTIO_GPU_PCI_H

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-gpu.h"

typedef struct VirtIOGPUPCIBase VirtIOGPUPCIBase;

/*
 * virtio-gpu-pci-base: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_GPU_PCI_BASE "virtio-gpu-pci-base"
#define VIRTIO_GPU_PCI_BASE(obj)                                    \
    OBJECT_CHECK(VirtIOGPUPCIBase, (obj), TYPE_VIRTIO_GPU_PCI_BASE)

struct VirtIOGPUPCIBase {
    VirtIOPCIProxy parent_obj;
    VirtIOGPUBase *vgpu;
};

/* to share between PCI and VGA */
#define DEFINE_VIRTIO_GPU_PCI_PROPERTIES(_state)                \
    DEFINE_PROP_BIT("ioeventfd", _state, flags,                 \
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, false),  \
        DEFINE_PROP_UINT32("vectors", _state, nvectors, 3)

#endif /* HW_VIRTIO_GPU_PCI_H */
