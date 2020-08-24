/*
 * vhost-user VGA device
 *
 * Copyright Red Hat, Inc. 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "virtio-vga.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_VGA "vhost-user-vga"

typedef struct VhostUserVGA VhostUserVGA;
DECLARE_INSTANCE_CHECKER(VhostUserVGA, VHOST_USER_VGA,
                         TYPE_VHOST_USER_VGA)

struct VhostUserVGA {
    VirtIOVGABase parent_obj;

    VhostUserGPU vdev;
};

static void vhost_user_vga_inst_initfn(Object *obj)
{
    VhostUserVGA *dev = VHOST_USER_VGA(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_GPU);

    VIRTIO_VGA_BASE(dev)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);

    object_property_add_alias(obj, "chardev",
                              OBJECT(&dev->vdev), "chardev");
}

static const VirtioPCIDeviceTypeInfo vhost_user_vga_info = {
    .generic_name  = TYPE_VHOST_USER_VGA,
    .parent        = TYPE_VIRTIO_VGA_BASE,
    .instance_size = sizeof(VhostUserVGA),
    .instance_init = vhost_user_vga_inst_initfn,
};

static void vhost_user_vga_register_types(void)
{
    virtio_pci_types_register(&vhost_user_vga_info);
}

type_init(vhost_user_vga_register_types)
