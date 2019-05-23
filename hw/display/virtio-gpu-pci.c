/*
 * Virtio video device
 *
 * Copyright Red Hat
 *
 * Authors:
 *  Dave Airlie
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-gpu-pci.h"

static Property virtio_gpu_pci_base_properties[] = {
    DEFINE_VIRTIO_GPU_PCI_PROPERTIES(VirtIOPCIProxy),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_gpu_pci_base_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOGPUPCIBase *vgpu = VIRTIO_GPU_PCI_BASE(vpci_dev);
    VirtIOGPUBase *g = vgpu->vgpu;
    DeviceState *vdev = DEVICE(g);
    int i;
    Error *local_error = NULL;

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (!virtio_pci_force_virtio_1(vpci_dev, errp)) {
        return;
    }
    object_property_set_bool(OBJECT(vdev), true, "realized", &local_error);

    if (local_error) {
        error_propagate(errp, local_error);
        return;
    }

    for (i = 0; i < g->conf.max_outputs; i++) {
        object_property_set_link(OBJECT(g->scanout[i].con),
                                 OBJECT(vpci_dev),
                                 "device", errp);
    }
}

static void virtio_gpu_pci_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->props = virtio_gpu_pci_base_properties;
    dc->hotpluggable = false;
    k->realize = virtio_gpu_pci_base_realize;
    pcidev_k->class_id = PCI_CLASS_DISPLAY_OTHER;
}

static const TypeInfo virtio_gpu_pci_base_info = {
    .name = TYPE_VIRTIO_GPU_PCI_BASE,
    .parent = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOGPUPCIBase),
    .class_init = virtio_gpu_pci_base_class_init,
    .abstract = true
};

#define TYPE_VIRTIO_GPU_PCI "virtio-gpu-pci"
#define VIRTIO_GPU_PCI(obj)                                 \
    OBJECT_CHECK(VirtIOGPUPCI, (obj), TYPE_VIRTIO_GPU_PCI)

typedef struct VirtIOGPUPCI {
    VirtIOGPUPCIBase parent_obj;
    VirtIOGPU vdev;
} VirtIOGPUPCI;

static void virtio_gpu_initfn(Object *obj)
{
    VirtIOGPUPCI *dev = VIRTIO_GPU_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU);
    VIRTIO_GPU_PCI_BASE(obj)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}

static const VirtioPCIDeviceTypeInfo virtio_gpu_pci_info = {
    .generic_name = TYPE_VIRTIO_GPU_PCI,
    .parent = TYPE_VIRTIO_GPU_PCI_BASE,
    .instance_size = sizeof(VirtIOGPUPCI),
    .instance_init = virtio_gpu_initfn,
};

static void virtio_gpu_pci_register_types(void)
{
    type_register_static(&virtio_gpu_pci_base_info);
    virtio_pci_types_register(&virtio_gpu_pci_info);
}

type_init(virtio_gpu_pci_register_types)
