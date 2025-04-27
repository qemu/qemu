/*
 * vhost ccw scsi implementation
 *
 * Copyright 2012, 2015 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
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
#include "hw/virtio/vhost-scsi.h"

#define TYPE_VHOST_SCSI_CCW "vhost-scsi-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VHostSCSICcw, VHOST_SCSI_CCW)

struct VHostSCSICcw {
    VirtioCcwDevice parent_obj;
    VHostSCSI vdev;
};

static void vhost_ccw_scsi_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VHostSCSICcw *dev = VHOST_SCSI_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void vhost_ccw_scsi_instance_init(Object *obj)
{
    VHostSCSICcw *dev = VHOST_SCSI_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_SCSI);
}

static const Property vhost_ccw_scsi_properties[] = {
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
};

static void vhost_ccw_scsi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = vhost_ccw_scsi_realize;
    device_class_set_props(dc, vhost_ccw_scsi_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo vhost_ccw_scsi = {
    .name          = TYPE_VHOST_SCSI_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VHostSCSICcw),
    .instance_init = vhost_ccw_scsi_instance_init,
    .class_init    = vhost_ccw_scsi_class_init,
};

static void virtio_ccw_scsi_register(void)
{
    type_register_static(&vhost_ccw_scsi);
}

type_init(virtio_ccw_scsi_register)
