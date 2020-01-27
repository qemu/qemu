/*
 * Vhost scsi PCI bindings
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * Changes for QEMU mainline + tcm_vhost kernel upstream:
 *  Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-scsi.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-pci.h"

typedef struct VHostSCSIPCI VHostSCSIPCI;

/*
 * vhost-scsi-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_SCSI_PCI "vhost-scsi-pci-base"
#define VHOST_SCSI_PCI(obj) \
        OBJECT_CHECK(VHostSCSIPCI, (obj), TYPE_VHOST_SCSI_PCI)

struct VHostSCSIPCI {
    VirtIOPCIProxy parent_obj;
    VHostSCSI vdev;
};

static Property vhost_scsi_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_scsi_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostSCSIPCI *dev = VHOST_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = vs->conf.num_queues + 3;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_scsi_pci_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, vhost_scsi_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vhost_scsi_pci_instance_init(Object *obj)
{
    VHostSCSIPCI *dev = VHOST_SCSI_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_SCSI);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
}

static const VirtioPCIDeviceTypeInfo vhost_scsi_pci_info = {
    .base_name             = TYPE_VHOST_SCSI_PCI,
    .generic_name          = "vhost-scsi-pci",
    .transitional_name     = "vhost-scsi-pci-transitional",
    .non_transitional_name = "vhost-scsi-pci-non-transitional",
    .instance_size = sizeof(VHostSCSIPCI),
    .instance_init = vhost_scsi_pci_instance_init,
    .class_init    = vhost_scsi_pci_class_init,
};

static void vhost_scsi_pci_register(void)
{
    virtio_pci_types_register(&vhost_scsi_pci_info);
}

type_init(vhost_scsi_pci_register)
