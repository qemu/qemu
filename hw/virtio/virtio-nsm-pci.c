/*
 * AWS Nitro Secure Module (NSM) device
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-nsm.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

typedef struct VirtIONsmPCI VirtIONsmPCI;

#define TYPE_VIRTIO_NSM_PCI "virtio-nsm-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIONsmPCI, VIRTIO_NSM_PCI,
                         TYPE_VIRTIO_NSM_PCI)

struct VirtIONsmPCI {
    VirtIOPCIProxy parent_obj;
    VirtIONSM vdev;
};

static void virtio_nsm_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIONsmPCI *vnsm = VIRTIO_NSM_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vnsm->vdev);

    virtio_pci_force_virtio_1(vpci_dev);

    if (!qdev_realize(vdev, BUS(&vpci_dev->bus), errp)) {
        return;
    }
}

static void virtio_nsm_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);

    k->realize = virtio_nsm_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void virtio_nsm_initfn(Object *obj)
{
    VirtIONsmPCI *dev = VIRTIO_NSM_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_NSM);
}

static const VirtioPCIDeviceTypeInfo virtio_nsm_pci_info = {
    .base_name             = TYPE_VIRTIO_NSM_PCI,
    .generic_name          = "virtio-nsm-pci",
    .instance_size = sizeof(VirtIONsmPCI),
    .instance_init = virtio_nsm_initfn,
    .class_init    = virtio_nsm_pci_class_init,
};

static void virtio_nsm_pci_register(void)
{
    virtio_pci_types_register(&virtio_nsm_pci_info);
}

type_init(virtio_nsm_pci_register)
