/*
 * Virtio PMEM PCI device
 *
 * Copyright (C) 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Pankaj Gupta <pagupta@redhat.com>
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pmem-pci.h"
#include "hw/mem/memory-device.h"
#include "qapi/error.h"

static void virtio_pmem_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOPMEMPCI *pmem_pci = VIRTIO_PMEM_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&pmem_pci->vdev);

    virtio_pci_force_virtio_1(vpci_dev);
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_pmem_pci_set_addr(MemoryDeviceState *md, uint64_t addr,
                                     Error **errp)
{
    object_property_set_uint(OBJECT(md), VIRTIO_PMEM_ADDR_PROP, addr, errp);
}

static uint64_t virtio_pmem_pci_get_addr(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_PMEM_ADDR_PROP,
                                    &error_abort);
}

static MemoryRegion *virtio_pmem_pci_get_memory_region(MemoryDeviceState *md,
                                                       Error **errp)
{
    VirtIOPMEMPCI *pci_pmem = VIRTIO_PMEM_PCI(md);
    VirtIOPMEM *pmem = &pci_pmem->vdev;
    VirtIOPMEMClass *vpc = VIRTIO_PMEM_GET_CLASS(pmem);

    return vpc->get_memory_region(pmem, errp);
}

static uint64_t virtio_pmem_pci_get_plugged_size(const MemoryDeviceState *md,
                                                 Error **errp)
{
    VirtIOPMEMPCI *pci_pmem = VIRTIO_PMEM_PCI(md);
    VirtIOPMEM *pmem = &pci_pmem->vdev;
    VirtIOPMEMClass *vpc = VIRTIO_PMEM_GET_CLASS(pmem);
    MemoryRegion *mr = vpc->get_memory_region(pmem, errp);

    /* the plugged size corresponds to the region size */
    return mr ? memory_region_size(mr) : 0;
}

static void virtio_pmem_pci_fill_device_info(const MemoryDeviceState *md,
                                             MemoryDeviceInfo *info)
{
    VirtioPMEMDeviceInfo *vi = g_new0(VirtioPMEMDeviceInfo, 1);
    VirtIOPMEMPCI *pci_pmem = VIRTIO_PMEM_PCI(md);
    VirtIOPMEM *pmem = &pci_pmem->vdev;
    VirtIOPMEMClass *vpc = VIRTIO_PMEM_GET_CLASS(pmem);
    DeviceState *dev = DEVICE(md);

    if (dev->id) {
        vi->id = g_strdup(dev->id);
    }

    /* let the real device handle everything else */
    vpc->fill_device_info(pmem, vi);

    info->u.virtio_pmem.data = vi;
    info->type = MEMORY_DEVICE_INFO_KIND_VIRTIO_PMEM;
}

static void virtio_pmem_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);

    k->realize = virtio_pmem_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;

    mdc->get_addr = virtio_pmem_pci_get_addr;
    mdc->set_addr = virtio_pmem_pci_set_addr;
    mdc->get_plugged_size = virtio_pmem_pci_get_plugged_size;
    mdc->get_memory_region = virtio_pmem_pci_get_memory_region;
    mdc->fill_device_info = virtio_pmem_pci_fill_device_info;
}

static void virtio_pmem_pci_instance_init(Object *obj)
{
    VirtIOPMEMPCI *dev = VIRTIO_PMEM_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_PMEM);
}

static const VirtioPCIDeviceTypeInfo virtio_pmem_pci_info = {
    .base_name             = TYPE_VIRTIO_PMEM_PCI,
    .generic_name          = "virtio-pmem-pci",
    .parent                = TYPE_VIRTIO_MD_PCI,
    .instance_size = sizeof(VirtIOPMEMPCI),
    .instance_init = virtio_pmem_pci_instance_init,
    .class_init    = virtio_pmem_pci_class_init,
};

static void virtio_pmem_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_pmem_pci_info);
}
type_init(virtio_pmem_pci_register_types)
