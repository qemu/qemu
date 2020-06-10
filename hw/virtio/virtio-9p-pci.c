/*
 * Virtio 9p PCI Bindings
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/9pfs/virtio-9p.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"

/*
 * virtio-9p-pci: This extends VirtioPCIProxy.
 */

#define TYPE_VIRTIO_9P_PCI "virtio-9p-pci-base"
#define VIRTIO_9P_PCI(obj) \
        OBJECT_CHECK(V9fsPCIState, (obj), TYPE_VIRTIO_9P_PCI)

typedef struct V9fsPCIState {
    VirtIOPCIProxy parent_obj;
    V9fsVirtioState vdev;
} V9fsPCIState;

static void virtio_9p_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    V9fsPCIState *dev = VIRTIO_9P_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static Property virtio_9p_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);

    k->realize = virtio_9p_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_9P;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = 0x2;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, virtio_9p_pci_properties);
}

static void virtio_9p_pci_instance_init(Object *obj)
{
    V9fsPCIState *dev = VIRTIO_9P_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_9P);
}

static const VirtioPCIDeviceTypeInfo virtio_9p_pci_info = {
    .base_name              = TYPE_VIRTIO_9P_PCI,
    .generic_name           = "virtio-9p-pci",
    .transitional_name      = "virtio-9p-pci-transitional",
    .non_transitional_name  = "virtio-9p-pci-non-transitional",
    .instance_size = sizeof(V9fsPCIState),
    .instance_init = virtio_9p_pci_instance_init,
    .class_init    = virtio_9p_pci_class_init,
};

static void virtio_9p_pci_register(void)
{
    virtio_pci_types_register(&virtio_9p_pci_info);
}

type_init(virtio_9p_pci_register)
