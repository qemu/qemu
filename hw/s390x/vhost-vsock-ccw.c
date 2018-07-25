/*
 * vhost vsock ccw implementation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio.h"
#include "qapi/error.h"
#include "virtio-ccw.h"

static Property vhost_vsock_ccw_properties[] = {
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_vsock_ccw_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VHostVSockCCWState *dev = VHOST_VSOCK_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&ccw_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_vsock_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = vhost_vsock_ccw_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = vhost_vsock_ccw_properties;
}

static void vhost_vsock_ccw_instance_init(Object *obj)
{
    VHostVSockCCWState *dev = VHOST_VSOCK_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VSOCK);
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
