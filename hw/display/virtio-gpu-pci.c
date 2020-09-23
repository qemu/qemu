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
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-gpu-pci.h"
#include "qom/object.h"

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

    virtio_pci_force_virtio_1(vpci_dev);
    if (!qdev_realize(vdev, BUS(&vpci_dev->bus), errp)) {
        return;
    }

    for (i = 0; i < g->conf.max_outputs; i++) {
        object_property_set_link(OBJECT(g->scanout[i].con), "device",
                                 OBJECT(vpci_dev), &error_abort);
    }
}

static void virtio_gpu_pci_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    device_class_set_props(dc, virtio_gpu_pci_base_properties);
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
typedef struct VirtIOGPUPCI VirtIOGPUPCI;
DECLARE_INSTANCE_CHECKER(VirtIOGPUPCI, VIRTIO_GPU_PCI,
                         TYPE_VIRTIO_GPU_PCI)

struct VirtIOGPUPCI {
    VirtIOGPUPCIBase parent_obj;
    VirtIOGPU vdev;
};

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
