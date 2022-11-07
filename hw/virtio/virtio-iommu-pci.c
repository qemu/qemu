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

#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/pci/pci_bus.h"
#include "qom/object.h"

typedef struct VirtIOIOMMUPCI VirtIOIOMMUPCI;

/*
 * virtio-iommu-pci: This extends VirtioPCIProxy.
 *
 */
DECLARE_INSTANCE_CHECKER(VirtIOIOMMUPCI, VIRTIO_IOMMU_PCI,
                         TYPE_VIRTIO_IOMMU_PCI)

struct VirtIOIOMMUPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOIOMMU vdev;
};

static Property virtio_iommu_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_ARRAY("reserved-regions", VirtIOIOMMUPCI,
                      vdev.nb_reserved_regions, vdev.reserved_regions,
                      qdev_prop_reserved_region, ReservedRegion),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_iommu_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOIOMMUPCI *dev = VIRTIO_IOMMU_PCI(vpci_dev);
    PCIBus *pbus = pci_get_bus(&vpci_dev->pci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(vdev);

    if (!qdev_get_machine_hotplug_handler(DEVICE(vpci_dev))) {
        error_setg(errp, "Check your machine implements a hotplug handler "
                         "for the virtio-iommu-pci device");
        return;
    }
    for (int i = 0; i < s->nb_reserved_regions; i++) {
        if (s->reserved_regions[i].type != VIRTIO_IOMMU_RESV_MEM_T_RESERVED &&
            s->reserved_regions[i].type != VIRTIO_IOMMU_RESV_MEM_T_MSI) {
            error_setg(errp, "reserved region %d has an invalid type", i);
            error_append_hint(errp, "Valid values are 0 and 1\n");
            return;
        }
    }
    if (!pci_bus_is_root(pbus)) {
        error_setg(errp, "virtio-iommu-pci must be plugged on the root bus");
        return;
    }

    object_property_set_link(OBJECT(dev), "primary-bus",
                             OBJECT(pbus), &error_abort);

    virtio_pci_force_virtio_1(vpci_dev);
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_iommu_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_iommu_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, virtio_iommu_pci_properties);
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
    .generic_name  = TYPE_VIRTIO_IOMMU_PCI,
    .instance_size = sizeof(VirtIOIOMMUPCI),
    .instance_init = virtio_iommu_pci_instance_init,
    .class_init    = virtio_iommu_pci_class_init,
};

static void virtio_iommu_pci_register(void)
{
    virtio_pci_types_register(&virtio_iommu_pci_info);
}

type_init(virtio_iommu_pci_register)


