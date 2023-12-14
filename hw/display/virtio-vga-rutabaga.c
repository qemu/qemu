/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-gpu.h"
#include "hw/display/vga.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-vga.h"
#include "qom/object.h"

#define TYPE_VIRTIO_VGA_RUTABAGA "virtio-vga-rutabaga"

OBJECT_DECLARE_SIMPLE_TYPE(VirtIOVGARutabaga, VIRTIO_VGA_RUTABAGA)

struct VirtIOVGARutabaga {
    VirtIOVGABase parent_obj;

    VirtIOGPURutabaga vdev;
};

static void virtio_vga_rutabaga_inst_initfn(Object *obj)
{
    VirtIOVGARutabaga *dev = VIRTIO_VGA_RUTABAGA(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU_RUTABAGA);
    VIRTIO_VGA_BASE(dev)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}

static VirtioPCIDeviceTypeInfo virtio_vga_rutabaga_info = {
    .generic_name  = TYPE_VIRTIO_VGA_RUTABAGA,
    .parent        = TYPE_VIRTIO_VGA_BASE,
    .instance_size = sizeof(VirtIOVGARutabaga),
    .instance_init = virtio_vga_rutabaga_inst_initfn,
};
module_obj(TYPE_VIRTIO_VGA_RUTABAGA);
module_kconfig(VIRTIO_VGA);

static void virtio_vga_register_types(void)
{
    if (have_vga) {
        virtio_pci_types_register(&virtio_vga_rutabaga_info);
    }
}

type_init(virtio_vga_register_types)

module_dep("hw-display-virtio-vga");
