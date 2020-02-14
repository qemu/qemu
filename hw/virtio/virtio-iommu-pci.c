/*
 * Virtio IOMMU PCI Bindings
 *
 * Copyright (c) 2019 Red Hat, Inc.
 * Written by Eric Auger
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "hw/boards.h"

typedef struct VirtIOIOMMUPCI VirtIOIOMMUPCI;

/*
 * virtio-iommu-pci: This extends VirtioPCIProxy.
 *
 */
#define VIRTIO_IOMMU_PCI(obj) \
        OBJECT_CHECK(VirtIOIOMMUPCI, (obj), TYPE_VIRTIO_IOMMU_PCI)

struct VirtIOIOMMUPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOIOMMU vdev;
};

static Property virtio_iommu_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_iommu_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOIOMMUPCI *dev = VIRTIO_IOMMU_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (!qdev_get_machine_hotplug_handler(DEVICE(vpci_dev))) {
        MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());

        error_setg(errp,
                   "%s machine fails to create iommu-map device tree bindings",
                   mc->name);
        error_append_hint(errp,
                          "Check you machine implements a hotplug handler "
                          "for the virtio-iommu-pci device\n");
        error_append_hint(errp, "Check the guest is booted without FW or with "
                          "-no-acpi\n");
        return;
    }
    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_link(OBJECT(dev),
                             OBJECT(pci_get_bus(&vpci_dev->pci_dev)),
                             "primary-bus", errp);
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_iommu_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_iommu_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, virtio_iommu_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_IOMMU;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
    dc->hotpluggable = false;
}

static void virtio_iommu_pci_instance_init(Object *obj)
{
    VirtIOIOMMUPCI *dev = VIRTIO_IOMMU_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_IOMMU);
}

static const VirtioPCIDeviceTypeInfo virtio_iommu_pci_info = {
    .base_name             = TYPE_VIRTIO_IOMMU_PCI,
    .generic_name          = "virtio-iommu-pci",
    .transitional_name     = "virtio-iommu-pci-transitional",
    .non_transitional_name = "virtio-iommu-pci-non-transitional",
    .instance_size = sizeof(VirtIOIOMMUPCI),
    .instance_init = virtio_iommu_pci_instance_init,
    .class_init    = virtio_iommu_pci_class_init,
};

static void virtio_iommu_pci_register(void)
{
    virtio_pci_types_register(&virtio_iommu_pci_info);
}

type_init(virtio_iommu_pci_register)


