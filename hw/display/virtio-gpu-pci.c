/*
 * Virtio video device
 *
 * Copyright Red Hat
 *
 * Authors:
 *  Dave Airlie
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#include "hw/pci/pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-gpu.h"

static Property virtio_gpu_pci_properties[] = {
    DEFINE_VIRTIO_GPU_PROPERTIES(VirtIOGPUPCI, vdev.conf),
    DEFINE_VIRTIO_GPU_PCI_PROPERTIES(VirtIOPCIProxy),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_gpu_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOGPUPCI *vgpu = VIRTIO_GPU_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vgpu->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    /* force virtio-1.0 */
    vpci_dev->flags &= ~VIRTIO_PCI_FLAG_DISABLE_MODERN;
    vpci_dev->flags |= VIRTIO_PCI_FLAG_DISABLE_LEGACY;
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_gpu_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->props = virtio_gpu_pci_properties;
    k->realize = virtio_gpu_pci_realize;
    pcidev_k->class_id = PCI_CLASS_DISPLAY_OTHER;
}

static void virtio_gpu_initfn(Object *obj)
{
    VirtIOGPUPCI *dev = VIRTIO_GPU_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_GPU);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static const TypeInfo virtio_gpu_pci_info = {
    .name = TYPE_VIRTIO_GPU_PCI,
    .parent = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOGPUPCI),
    .instance_init = virtio_gpu_initfn,
    .class_init = virtio_gpu_pci_class_init,
};

static void virtio_gpu_pci_register_types(void)
{
    type_register_static(&virtio_gpu_pci_info);
}
type_init(virtio_gpu_pci_register_types)
