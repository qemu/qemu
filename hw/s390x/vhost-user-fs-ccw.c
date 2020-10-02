/*
 * virtio ccw vhost-user-fs implementation
 *
 * Copyright 2020 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "hw/virtio/vhost-user-fs.h"
#include "virtio-ccw.h"

typedef struct VHostUserFSCcw {
    VirtioCcwDevice parent_obj;
    VHostUserFS vdev;
} VHostUserFSCcw;

#define TYPE_VHOST_USER_FS_CCW "vhost-user-fs-ccw"
#define VHOST_USER_FS_CCW(obj) \
        OBJECT_CHECK(VHostUserFSCcw, (obj), TYPE_VHOST_USER_FS_CCW)


static Property vhost_user_fs_ccw_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_fs_ccw_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VHostUserFSCcw *dev = VHOST_USER_FS_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void vhost_user_fs_ccw_instance_init(Object *obj)
{
    VHostUserFSCcw *dev = VHOST_USER_FS_CCW(obj);
    VirtioCcwDevice *ccw_dev = VIRTIO_CCW_DEVICE(obj);

    ccw_dev->force_revision_1 = true;
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_FS);
}

static void vhost_user_fs_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = vhost_user_fs_ccw_realize;
    device_class_set_props(dc, vhost_user_fs_ccw_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo vhost_user_fs_ccw = {
    .name          = TYPE_VHOST_USER_FS_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VHostUserFSCcw),
    .instance_init = vhost_user_fs_ccw_instance_init,
    .class_init    = vhost_user_fs_ccw_class_init,
};

static void vhost_user_fs_ccw_register(void)
{
    type_register_static(&vhost_user_fs_ccw);
}

type_init(vhost_user_fs_ccw_register)
