/*
 * vhost vsock ccw implementation
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
#include "hw/virtio/vhost-vsock.h"

#define TYPE_VHOST_VSOCK_CCW "vhost-vsock-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VHostVSockCCWState, VHOST_VSOCK_CCW)

struct VHostVSockCCWState {
    VirtioCcwDevice parent_obj;
    VHostVSock vdev;
};

static const Property vhost_vsock_ccw_properties[] = {
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
};

static void vhost_vsock_ccw_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VHostVSockCCWState *dev = VHOST_VSOCK_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void vhost_vsock_ccw_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = vhost_vsock_ccw_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, vhost_vsock_ccw_properties);
}

static void vhost_vsock_ccw_instance_init(Object *obj)
{
    VHostVSockCCWState *dev = VHOST_VSOCK_CCW(obj);
    VirtioCcwDevice *ccw_dev = VIRTIO_CCW_DEVICE(obj);
    VirtIODevice *virtio_dev;

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VSOCK);

    virtio_dev = VIRTIO_DEVICE(&dev->vdev);

    /*
     * To avoid migration issues, we force virtio version 1 only when
     * legacy check is enabled in the new machine types (>= 5.1).
     */
    if (!virtio_legacy_check_disabled(virtio_dev)) {
        ccw_dev->force_revision_1 = true;
    }
}

static const TypeInfo vhost_vsock_ccw_info = {
    .name          = TYPE_VHOST_VSOCK_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VHostVSockCCWState),
    .instance_init = vhost_vsock_ccw_instance_init,
    .class_init    = vhost_vsock_ccw_class_init,
};

static void vhost_vsock_ccw_register(void)
{
    type_register_static(&vhost_vsock_ccw_info);
}

type_init(vhost_vsock_ccw_register)
