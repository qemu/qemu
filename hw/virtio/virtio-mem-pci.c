/*
 * Virtio MEM PCI device
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "virtio-mem-pci.h"
#include "hw/mem/memory-device.h"
#include "qapi/error.h"
#include "qapi/qapi-events-machine.h"
#include "qapi/qapi-events-misc.h"

static void virtio_mem_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOMEMPCI *mem_pci = VIRTIO_MEM_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&mem_pci->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = 2;
    }

    virtio_pci_force_virtio_1(vpci_dev);
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_mem_pci_set_addr(MemoryDeviceState *md, uint64_t addr,
                                    Error **errp)
{
    object_property_set_uint(OBJECT(md), VIRTIO_MEM_ADDR_PROP, addr, errp);
}

static uint64_t virtio_mem_pci_get_addr(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_ADDR_PROP,
                                    &error_abort);
}

static MemoryRegion *virtio_mem_pci_get_memory_region(MemoryDeviceState *md,
                                                      Error **errp)
{
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(md);
    VirtIOMEM *vmem = &pci_mem->vdev;
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    return vmc->get_memory_region(vmem, errp);
}

static void virtio_mem_pci_decide_memslots(MemoryDeviceState *md,
                                           unsigned int limit)
{
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(md);
    VirtIOMEM *vmem = VIRTIO_MEM(&pci_mem->vdev);
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    vmc->decide_memslots(vmem, limit);
}

static unsigned int virtio_mem_pci_get_memslots(MemoryDeviceState *md)
{
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(md);
    VirtIOMEM *vmem = VIRTIO_MEM(&pci_mem->vdev);
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    return vmc->get_memslots(vmem);
}

static uint64_t virtio_mem_pci_get_plugged_size(const MemoryDeviceState *md,
                                                Error **errp)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_SIZE_PROP,
                                    errp);
}

static void virtio_mem_pci_fill_device_info(const MemoryDeviceState *md,
                                            MemoryDeviceInfo *info)
{
    VirtioMEMDeviceInfo *vi = g_new0(VirtioMEMDeviceInfo, 1);
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(md);
    VirtIOMEM *vmem = &pci_mem->vdev;
    VirtIOMEMClass *vpc = VIRTIO_MEM_GET_CLASS(vmem);
    DeviceState *dev = DEVICE(md);

    if (dev->id) {
        vi->id = g_strdup(dev->id);
    }

    /* let the real device handle everything else */
    vpc->fill_device_info(vmem, vi);

    info->u.virtio_mem.data = vi;
    info->type = MEMORY_DEVICE_INFO_KIND_VIRTIO_MEM;
}

static uint64_t virtio_mem_pci_get_min_alignment(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_BLOCK_SIZE_PROP,
                                    &error_abort);
}

static void virtio_mem_pci_size_change_notify(Notifier *notifier, void *data)
{
    VirtIOMEMPCI *pci_mem = container_of(notifier, VirtIOMEMPCI,
                                         size_change_notifier);
    DeviceState *dev = DEVICE(pci_mem);
    char *qom_path = object_get_canonical_path(OBJECT(dev));
    const uint64_t * const size_p = data;

    qapi_event_send_memory_device_size_change(dev->id, *size_p, qom_path);
    g_free(qom_path);
}

static void virtio_mem_pci_unplug_request_check(VirtIOMDPCI *vmd, Error **errp)
{
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(vmd);
    VirtIOMEM *vmem = &pci_mem->vdev;
    VirtIOMEMClass *vpc = VIRTIO_MEM_GET_CLASS(vmem);

    vpc->unplug_request_check(vmem, errp);
}

static void virtio_mem_pci_get_requested_size(Object *obj, Visitor *v,
                                              const char *name, void *opaque,
                                              Error **errp)
{
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(obj);

    object_property_get(OBJECT(&pci_mem->vdev), name, v, errp);
}

static void virtio_mem_pci_set_requested_size(Object *obj, Visitor *v,
                                              const char *name, void *opaque,
                                              Error **errp)
{
    VirtIOMEMPCI *pci_mem = VIRTIO_MEM_PCI(obj);
    DeviceState *dev = DEVICE(obj);

    /*
     * If we passed virtio_mem_pci_unplug_request_check(), making sure that
     * the requested size is 0, don't allow modifying the requested size
     * anymore, otherwise the VM might end up hotplugging memory before
     * handling the unplug request.
     */
    if (dev->pending_deleted_event) {
        error_setg(errp, "'%s' cannot be changed if the device is in the"
                   " process of unplug", name);
        return;
    }

    object_property_set(OBJECT(&pci_mem->vdev), name, v, errp);
}

static const Property virtio_mem_pci_class_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
};

static void virtio_mem_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);
    VirtIOMDPCIClass *vmdc = VIRTIO_MD_PCI_CLASS(klass);

    k->realize = virtio_mem_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
    device_class_set_props(dc, virtio_mem_pci_class_properties);

    mdc->get_addr = virtio_mem_pci_get_addr;
    mdc->set_addr = virtio_mem_pci_set_addr;
    mdc->get_plugged_size = virtio_mem_pci_get_plugged_size;
    mdc->get_memory_region = virtio_mem_pci_get_memory_region;
    mdc->decide_memslots = virtio_mem_pci_decide_memslots;
    mdc->get_memslots = virtio_mem_pci_get_memslots;
    mdc->fill_device_info = virtio_mem_pci_fill_device_info;
    mdc->get_min_alignment = virtio_mem_pci_get_min_alignment;

    vmdc->unplug_request_check = virtio_mem_pci_unplug_request_check;
}

static void virtio_mem_pci_instance_init(Object *obj)
{
    VirtIOMEMPCI *dev = VIRTIO_MEM_PCI(obj);
    VirtIOMEMClass *vmc;
    VirtIOMEM *vmem;

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_MEM);

    dev->size_change_notifier.notify = virtio_mem_pci_size_change_notify;
    vmem = &dev->vdev;
    vmc = VIRTIO_MEM_GET_CLASS(vmem);
    /*
     * We never remove the notifier again, as we expect both devices to
     * disappear at the same time.
     */
    vmc->add_size_change_notifier(vmem, &dev->size_change_notifier);

    object_property_add_alias(obj, VIRTIO_MEM_BLOCK_SIZE_PROP,
                              OBJECT(&dev->vdev), VIRTIO_MEM_BLOCK_SIZE_PROP);
    object_property_add_alias(obj, VIRTIO_MEM_SIZE_PROP, OBJECT(&dev->vdev),
                              VIRTIO_MEM_SIZE_PROP);
    object_property_add(obj, VIRTIO_MEM_REQUESTED_SIZE_PROP, "size",
                        virtio_mem_pci_get_requested_size,
                        virtio_mem_pci_set_requested_size, NULL, NULL);
}

static const VirtioPCIDeviceTypeInfo virtio_mem_pci_info = {
    .base_name = TYPE_VIRTIO_MEM_PCI,
    .parent = TYPE_VIRTIO_MD_PCI,
    .generic_name = "virtio-mem-pci",
    .instance_size = sizeof(VirtIOMEMPCI),
    .instance_init = virtio_mem_pci_instance_init,
    .class_init = virtio_mem_pci_class_init,
};

static void virtio_mem_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_mem_pci_info);
}
type_init(virtio_mem_pci_register_types)
