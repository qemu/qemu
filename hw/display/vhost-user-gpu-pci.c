/*
 * vhost-user GPU PCI device
 *
 * Copyright Red Hat, Inc. 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-gpu-pci.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_GPU_PCI "vhost-user-gpu-pci"
typedef struct VhostUserGPUPCI VhostUserGPUPCI;
DECLARE_INSTANCE_CHECKER(VhostUserGPUPCI, VHOST_USER_GPU_PCI,
                         TYPE_VHOST_USER_GPU_PCI)

struct VhostUserGPUPCI {
    VirtIOGPUPCIBase parent_obj;

    VhostUserGPU vdev;
};

static void vhost_user_gpu_pci_initfn(Object *obj)
{
    VhostUserGPUPCI *dev = VHOST_USER_GPU_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_GPU);

    VIRTIO_GPU_PCI_BASE(obj)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);

    object_property_add_alias(obj, "chardev",
                              OBJECT(&dev->vdev), "chardev");
}

static const VirtioPCIDeviceTypeInfo vhost_user_gpu_pci_info = {
    .generic_name = TYPE_VHOST_USER_GPU_PCI,
    .parent = TYPE_VIRTIO_GPU_PCI_BASE,
    .instance_size = sizeof(VhostUserGPUPCI),
    .instance_init = vhost_user_gpu_pci_initfn,
};
module_obj(TYPE_VHOST_USER_GPU_PCI);

static void vhost_user_gpu_pci_register_types(void)
{
    virtio_pci_types_register(&vhost_user_gpu_pci_info);
}

type_init(vhost_user_gpu_pci_register_types)
