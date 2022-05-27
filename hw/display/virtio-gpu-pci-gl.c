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

#define TYPE_VIRTIO_GPU_GL_PCI "virtio-gpu-gl-pci"
typedef struct VirtIOGPUGLPCI VirtIOGPUGLPCI;
DECLARE_INSTANCE_CHECKER(VirtIOGPUGLPCI, VIRTIO_GPU_GL_PCI,
                         TYPE_VIRTIO_GPU_GL_PCI)

struct VirtIOGPUGLPCI {
    VirtIOGPUPCIBase parent_obj;
    VirtIOGPUGL vdev;
};

static void virtio_gpu_gl_initfn(Object *obj)
{
    VirtIOGPUGLPCI *dev = VIRTIO_GPU_GL_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU_GL);
    VIRTIO_GPU_PCI_BASE(obj)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}

static const VirtioPCIDeviceTypeInfo virtio_gpu_gl_pci_info = {
    .generic_name = TYPE_VIRTIO_GPU_GL_PCI,
    .parent = TYPE_VIRTIO_GPU_PCI_BASE,
    .instance_size = sizeof(VirtIOGPUGLPCI),
    .instance_init = virtio_gpu_gl_initfn,
};
module_obj(TYPE_VIRTIO_GPU_GL_PCI);
module_kconfig(VIRTIO_PCI);

static void virtio_gpu_gl_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_gpu_gl_pci_info);
}

type_init(virtio_gpu_gl_pci_register_types)

module_dep("hw-display-virtio-gpu-pci");
