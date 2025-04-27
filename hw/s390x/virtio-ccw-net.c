/*
 * virtio ccw net implementation
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
#include "hw/virtio/virtio-net.h"

#define TYPE_VIRTIO_NET_CCW "virtio-net-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIONetCcw, VIRTIO_NET_CCW)

struct VirtIONetCcw {
    VirtioCcwDevice parent_obj;
    VirtIONet vdev;
};

static void virtio_ccw_net_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    DeviceState *qdev = DEVICE(ccw_dev);
    VirtIONetCcw *dev = VIRTIO_NET_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    virtio_net_set_netclient_name(&dev->vdev, qdev->id,
                                  object_get_typename(OBJECT(qdev)));
    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_net_instance_init(Object *obj)
{
    VirtIONetCcw *dev = VIRTIO_NET_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_NET);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex");
}

static const Property virtio_ccw_net_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_CCW_LOADPARM("loadparm", CcwDevice, loadparm),
};

static void virtio_ccw_net_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_net_realize;
    device_class_set_props(dc, virtio_ccw_net_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo virtio_ccw_net = {
    .name          = TYPE_VIRTIO_NET_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIONetCcw),
    .instance_init = virtio_ccw_net_instance_init,
    .class_init    = virtio_ccw_net_class_init,
};

static void virtio_ccw_net_register(void)
{
    type_register_static(&virtio_ccw_net);
}

type_init(virtio_ccw_net_register)
