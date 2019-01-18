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
#include "hw/pci/pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-gpu.h"

typedef struct VirtIOGPUPCI VirtIOGPUPCI;

/*
 * virtio-gpu-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_GPU_PCI "virtio-gpu-pci"
#define VIRTIO_GPU_PCI(obj) \
        OBJECT_CHECK(VirtIOGPUPCI, (obj), TYPE_VIRTIO_GPU_PCI)

struct VirtIOGPUPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOGPU vdev;
};

static Property virtio_gpu_pci_properties[] = {
    DEFINE_VIRTIO_GPU_PCI_PROPERTIES(VirtIOPCIProxy),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_gpu_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOGPUPCI *vgpu = VIRTIO_GPU_PCI(vpci_dev);
    VirtIOGPU *g = &vgpu->vdev;
    DeviceState *vdev = DEVICE(&vgpu->vdev);
    int i;
    Error *local_error = NULL;

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    virtio_pci_force_virtio_1(vpci_dev);
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

static void virtio_gpu_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->props = virtio_gpu_pci_properties;
    dc->hotpluggable = false;
    k->realize = virtio_gpu_pci_realize;
    pcidev_k->class_id = PCI_CLASS_DISPLAY_OTHER;
}

static void virtio_gpu_initfn(Object *obj)
{
    VirtIOGPUPCI *dev = VIRTIO_GPU_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU);
}

static const VirtioPCIDeviceTypeInfo virtio_gpu_pci_info = {
    .generic_name = TYPE_VIRTIO_GPU_PCI,
    .instance_size = sizeof(VirtIOGPUPCI),
    .instance_init = virtio_gpu_initfn,
    .class_init = virtio_gpu_pci_class_init,
};

static void virtio_gpu_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_gpu_pci_info);
}
type_init(virtio_gpu_pci_register_types)
