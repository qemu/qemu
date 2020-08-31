/*
 * Virtio rng PCI Bindings
 *
 * Copyright 2012 Red Hat, Inc.
 * Copyright 2012 Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/virtio/virtio-rng.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qom/object.h"

typedef struct VirtIORngPCI VirtIORngPCI;

/*
 * virtio-rng-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_RNG_PCI "virtio-rng-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIORngPCI, VIRTIO_RNG_PCI,
                         TYPE_VIRTIO_RNG_PCI)

struct VirtIORngPCI {
    VirtIOPCIProxy parent_obj;
    VirtIORNG vdev;
};

static void virtio_rng_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIORngPCI *vrng = VIRTIO_RNG_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vrng->vdev);

    if (!qdev_realize(vdev, BUS(&vpci_dev->bus), errp)) {
        return;
    }
}

static void virtio_rng_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_rng_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_RNG;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_rng_initfn(Object *obj)
{
    VirtIORngPCI *dev = VIRTIO_RNG_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_RNG);
}

static const VirtioPCIDeviceTypeInfo virtio_rng_pci_info = {
    .base_name             = TYPE_VIRTIO_RNG_PCI,
    .generic_name          = "virtio-rng-pci",
    .transitional_name     = "virtio-rng-pci-transitional",
    .non_transitional_name = "virtio-rng-pci-non-transitional",
    .instance_size = sizeof(VirtIORngPCI),
    .instance_init = virtio_rng_initfn,
    .class_init    = virtio_rng_pci_class_init,
};

static void virtio_rng_pci_register(void)
{
    virtio_pci_types_register(&virtio_rng_pci_info);
}

type_init(virtio_rng_pci_register)
