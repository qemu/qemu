/*
 * Vhost user scsi PCI Bindings
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 *
 * Author:
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This work is largely based on the "vhost-scsi" implementation by:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/vhost-user-scsi.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/loader.h"
#include "system/kvm.h"
#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"

typedef struct VHostUserSCSIPCI VHostUserSCSIPCI;

#define TYPE_VHOST_USER_SCSI_PCI "vhost-user-scsi-pci-base"
DECLARE_INSTANCE_CHECKER(VHostUserSCSIPCI, VHOST_USER_SCSI_PCI,
                         TYPE_VHOST_USER_SCSI_PCI)

struct VHostUserSCSIPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserSCSI vdev;
};

static const Property vhost_user_scsi_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
};

static void vhost_user_scsi_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserSCSIPCI *dev = VHOST_USER_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSCSIConf *conf = &dev->vdev.parent_obj.parent_obj.conf;

    if (conf->num_queues == VIRTIO_SCSI_AUTO_NUM_QUEUES) {
        conf->num_queues =
            virtio_pci_optimal_num_queues(VIRTIO_SCSI_VQ_NUM_FIXED);
    }

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = conf->num_queues + VIRTIO_SCSI_VQ_NUM_FIXED + 1;
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_scsi_pci_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, vhost_user_scsi_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vhost_user_scsi_pci_instance_init(Object *obj)
{
    VHostUserSCSIPCI *dev = VHOST_USER_SCSI_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_SCSI);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static const VirtioPCIDeviceTypeInfo vhost_user_scsi_pci_info = {
    .base_name             = TYPE_VHOST_USER_SCSI_PCI,
    .generic_name          = "vhost-user-scsi-pci",
    .transitional_name     = "vhost-user-scsi-pci-transitional",
    .non_transitional_name = "vhost-user-scsi-pci-non-transitional",
    .instance_size = sizeof(VHostUserSCSIPCI),
    .instance_init = vhost_user_scsi_pci_instance_init,
    .class_init    = vhost_user_scsi_pci_class_init,
};

static void vhost_user_scsi_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_scsi_pci_info);
}

type_init(vhost_user_scsi_pci_register)
