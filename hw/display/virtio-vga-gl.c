#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-gpu.h"
#include "hw/display/vga.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-vga.h"
#include "qom/object.h"

#define TYPE_VIRTIO_VGA_GL "virtio-vga-gl"

typedef struct VirtIOVGAGL VirtIOVGAGL;
DECLARE_INSTANCE_CHECKER(VirtIOVGAGL, VIRTIO_VGA_GL,
                         TYPE_VIRTIO_VGA_GL)

struct VirtIOVGAGL {
    VirtIOVGABase parent_obj;

    VirtIOGPUGL   vdev;
};

static void virtio_vga_gl_inst_initfn(Object *obj)
{
    VirtIOVGAGL *dev = VIRTIO_VGA_GL(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU_GL);
    VIRTIO_VGA_BASE(dev)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}


static VirtioPCIDeviceTypeInfo virtio_vga_gl_info = {
    .generic_name  = TYPE_VIRTIO_VGA_GL,
    .parent        = TYPE_VIRTIO_VGA_BASE,
    .instance_size = sizeof(VirtIOVGAGL),
    .instance_init = virtio_vga_gl_inst_initfn,
};
module_obj(TYPE_VIRTIO_VGA_GL);
module_kconfig(VIRTIO_VGA);

static void virtio_vga_register_types(void)
{
    if (have_vga) {
        virtio_pci_types_register(&virtio_vga_gl_info);
    }
}

type_init(virtio_vga_register_types)

module_dep("hw-display-virtio-vga");
