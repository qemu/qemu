/*
 * Vhost user blk PCI Bindings
 *
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * Largely based on the "vhost-user-scsi.c" and "vhost-scsi.c" implemented by:
 * Felipe Franciosi <felipe@nutanix.com>
 * Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 * Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost-user-blk.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "virtio-pci.h"

typedef struct VHostUserBlkPCI VHostUserBlkPCI;

/*
 * vhost-user-blk-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_USER_BLK_PCI "vhost-user-blk-pci-base"
#define VHOST_USER_BLK_PCI(obj) \
        OBJECT_CHECK(VHostUserBlkPCI, (obj), TYPE_VHOST_USER_BLK_PCI)

struct VHostUserBlkPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserBlk vdev;
};

static Property vhost_user_blk_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_blk_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserBlkPCI *dev = VHOST_USER_BLK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = dev->vdev.num_queues + 1;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_user_blk_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = vhost_user_blk_pci_properties;
    k->realize = vhost_user_blk_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vhost_user_blk_pci_instance_init(Object *obj)
{
    VHostUserBlkPCI *dev = VHOST_USER_BLK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_BLK);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
}

static const VirtioPCIDeviceTypeInfo vhost_user_blk_pci_info = {
    .base_name               = TYPE_VHOST_USER_BLK_PCI,
    .generic_name            = "vhost-user-blk-pci",
    .transitional_name       = "vhost-user-blk-pci-transitional",
    .non_transitional_name   = "vhost-user-blk-pci-non-transitional",
    .instance_size  = sizeof(VHostUserBlkPCI),
    .instance_init  = vhost_user_blk_pci_instance_init,
    .class_init     = vhost_user_blk_pci_class_init,
};

static void vhost_user_blk_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_blk_pci_info);
}

type_init(vhost_user_blk_pci_register)
