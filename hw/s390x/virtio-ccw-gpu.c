/*
 * virtio ccw gpu implementation
 *
 * Copyright 2012, 2015 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-ccw.h"
#include "hw/virtio/virtio-gpu.h"

#define TYPE_VIRTIO_GPU_CCW "virtio-gpu-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOGPUCcw, VIRTIO_GPU_CCW)

struct VirtIOGPUCcw {
    VirtioCcwDevice parent_obj;
    VirtIOGPU vdev;
};

static void virtio_ccw_gpu_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOGPUCcw *dev = VIRTIO_GPU_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_gpu_instance_init(Object *obj)
{
    VirtIOGPUCcw *dev = VIRTIO_GPU_CCW(obj);
    VirtioCcwDevice *ccw_dev = VIRTIO_CCW_DEVICE(obj);

    ccw_dev->force_revision_1 = true;
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU);
}

static const Property virtio_ccw_gpu_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
};

static void virtio_ccw_gpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_gpu_realize;
    device_class_set_props(dc, virtio_ccw_gpu_properties);
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo virtio_ccw_gpu = {
    .name          = TYPE_VIRTIO_GPU_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIOGPUCcw),
    .instance_init = virtio_ccw_gpu_instance_init,
    .class_init    = virtio_ccw_gpu_class_init,
};
module_obj(TYPE_VIRTIO_GPU_CCW);
module_kconfig(VIRTIO_CCW);

static void virtio_ccw_gpu_register(void)
{
    if (have_virtio_ccw) {
        type_register_static(&virtio_ccw_gpu);
    }
}

type_init(virtio_ccw_gpu_register)

module_arch("s390x");
