/*
 * virtio ccw block implementation
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
#include "hw/virtio/virtio-blk.h"

#define TYPE_VIRTIO_BLK_CCW "virtio-blk-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOBlkCcw, VIRTIO_BLK_CCW)

struct VirtIOBlkCcw {
    VirtioCcwDevice parent_obj;
    VirtIOBlock vdev;
};

static void virtio_ccw_blk_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOBlkCcw *dev = VIRTIO_BLK_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_blk_instance_init(Object *obj)
{
    VirtIOBlkCcw *dev = VIRTIO_BLK_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_BLK);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static const Property virtio_ccw_blk_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_CCW_LOADPARM("loadparm", CcwDevice, loadparm),
};

static void virtio_ccw_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_blk_realize;
    device_class_set_props(dc, virtio_ccw_blk_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo virtio_ccw_blk = {
    .name          = TYPE_VIRTIO_BLK_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIOBlkCcw),
    .instance_init = virtio_ccw_blk_instance_init,
    .class_init    = virtio_ccw_blk_class_init,
};

static void virtio_ccw_blk_register(void)
{
    type_register_static(&virtio_ccw_blk);
}

type_init(virtio_ccw_blk_register)
