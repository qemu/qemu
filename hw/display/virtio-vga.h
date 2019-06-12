#ifndef VIRTIO_VGA_H
#define VIRTIO_VGA_H

#include "hw/virtio/virtio-gpu-pci.h"
#include "vga_int.h"

/*
 * virtio-vga-base: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_VGA_BASE "virtio-vga-base"
#define VIRTIO_VGA_BASE(obj)                                \
    OBJECT_CHECK(VirtIOVGABase, (obj), TYPE_VIRTIO_VGA_BASE)
#define VIRTIO_VGA_BASE_GET_CLASS(obj)                      \
    OBJECT_GET_CLASS(VirtIOVGABaseClass, obj, TYPE_VIRTIO_VGA_BASE)
#define VIRTIO_VGA_BASE_CLASS(klass)                        \
    OBJECT_CLASS_CHECK(VirtIOVGABaseClass, klass, TYPE_VIRTIO_VGA_BASE)

typedef struct VirtIOVGABase {
    VirtIOPCIProxy parent_obj;

    VirtIOGPUBase *vgpu;
    VGACommonState vga;
    MemoryRegion vga_mrs[3];
} VirtIOVGABase;

typedef struct VirtIOVGABaseClass {
    VirtioPCIClass parent_class;

    DeviceReset parent_reset;
} VirtIOVGABaseClass;

#endif /* VIRTIO_VGA_H */
