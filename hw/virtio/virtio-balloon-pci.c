/*
 * Virtio balloon PCI Bindings
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2009 CodeSourcery
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paul Brook        <paul@codesourcery.com>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-balloon.h"
#include "qapi/error.h"
#include "qemu/module.h"

typedef struct VirtIOBalloonPCI VirtIOBalloonPCI;

/*
 * virtio-balloon-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_BALLOON_PCI "virtio-balloon-pci-base"
#define VIRTIO_BALLOON_PCI(obj) \
        OBJECT_CHECK(VirtIOBalloonPCI, (obj), TYPE_VIRTIO_BALLOON_PCI)

struct VirtIOBalloonPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOBalloon vdev;
};
static Property virtio_balloon_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_balloon_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOBalloonPCI *dev = VIRTIO_BALLOON_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->class_code != PCI_CLASS_OTHERS &&
        vpci_dev->class_code != PCI_CLASS_MEMORY_RAM) { /* qemu < 1.1 */
        vpci_dev->class_code = PCI_CLASS_OTHERS;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_balloon_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_balloon_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = virtio_balloon_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BALLOON;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_balloon_pci_instance_init(Object *obj)
{
    VirtIOBalloonPCI *dev = VIRTIO_BALLOON_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_BALLOON);
    object_property_add_alias(obj, "guest-stats", OBJECT(&dev->vdev),
                                  "guest-stats", &error_abort);
    object_property_add_alias(obj, "guest-stats-polling-interval",
                              OBJECT(&dev->vdev),
                              "guest-stats-polling-interval", &error_abort);
}

static const VirtioPCIDeviceTypeInfo virtio_balloon_pci_info = {
    .base_name             = TYPE_VIRTIO_BALLOON_PCI,
    .generic_name          = "virtio-balloon-pci",
    .transitional_name     = "virtio-balloon-pci-transitional",
    .non_transitional_name = "virtio-balloon-pci-non-transitional",
    .instance_size = sizeof(VirtIOBalloonPCI),
    .instance_init = virtio_balloon_pci_instance_init,
    .class_init    = virtio_balloon_pci_class_init,
};

static void virtio_balloon_pci_register(void)
{
    virtio_pci_types_register(&virtio_balloon_pci_info);
}

type_init(virtio_balloon_pci_register)
