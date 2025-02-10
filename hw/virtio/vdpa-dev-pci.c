/*
 * Vhost Vdpa Device PCI Bindings
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All Rights Reserved.
 *
 * Authors:
 *   Longpeng <longpeng2@huawei.com>
 *
 * Largely based on the "vhost-user-blk-pci.c" and "vhost-user-blk.c"
 * implemented by:
 *   Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vhost.h>
#include "hw/virtio/virtio.h"
#include "hw/virtio/vdpa-dev.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"


typedef struct VhostVdpaDevicePCI VhostVdpaDevicePCI;

#define TYPE_VHOST_VDPA_DEVICE_PCI "vhost-vdpa-device-pci-base"
DECLARE_INSTANCE_CHECKER(VhostVdpaDevicePCI, VHOST_VDPA_DEVICE_PCI,
                         TYPE_VHOST_VDPA_DEVICE_PCI)

struct VhostVdpaDevicePCI {
    VirtIOPCIProxy parent_obj;
    VhostVdpaDevice vdev;
};

static void vhost_vdpa_device_pci_instance_init(Object *obj)
{
    VhostVdpaDevicePCI *dev = VHOST_VDPA_DEVICE_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VDPA_DEVICE);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static int vhost_vdpa_device_pci_post_init(VhostVdpaDevice *v, Error **errp)
{
    VhostVdpaDevicePCI *dev = container_of(v, VhostVdpaDevicePCI, vdev);
    VirtIOPCIProxy *vpci_dev = &dev->parent_obj;

    vpci_dev->class_code = virtio_pci_get_class_id(v->vdev_id);
    vpci_dev->trans_devid = virtio_pci_get_trans_devid(v->vdev_id);
    /* one for config vector */
    vpci_dev->nvectors = v->num_queues + 1;

    return 0;
}

static void
vhost_vdpa_device_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VhostVdpaDevicePCI *dev = VHOST_VDPA_DEVICE_PCI(vpci_dev);

    dev->vdev.post_init = vhost_vdpa_device_pci_post_init;
    qdev_realize(DEVICE(&dev->vdev), BUS(&vpci_dev->bus), errp);
}

static void vhost_vdpa_device_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    k->realize = vhost_vdpa_device_pci_realize;
}

static const VirtioPCIDeviceTypeInfo vhost_vdpa_device_pci_info = {
    .base_name               = TYPE_VHOST_VDPA_DEVICE_PCI,
    .generic_name            = "vhost-vdpa-device-pci",
    .transitional_name       = "vhost-vdpa-device-pci-transitional",
    .non_transitional_name   = "vhost-vdpa-device-pci-non-transitional",
    .instance_size  = sizeof(VhostVdpaDevicePCI),
    .instance_init  = vhost_vdpa_device_pci_instance_init,
    .class_init     = vhost_vdpa_device_pci_class_init,
};

static void vhost_vdpa_device_pci_register(void)
{
    virtio_pci_types_register(&vhost_vdpa_device_pci_info);
}

type_init(vhost_vdpa_device_pci_register);
