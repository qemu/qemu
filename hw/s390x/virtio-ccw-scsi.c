/*
 * virtio ccw scsi implementation
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
#include "hw/virtio/virtio-scsi.h"

#define TYPE_VIRTIO_SCSI_CCW "virtio-scsi-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOSCSICcw, VIRTIO_SCSI_CCW)

struct VirtIOSCSICcw {
    VirtioCcwDevice parent_obj;
    VirtIOSCSI vdev;
};

static void virtio_ccw_scsi_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOSCSICcw *dev = VIRTIO_SCSI_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *qdev = DEVICE(ccw_dev);
    char *bus_name;

    /*
     * For command line compatibility, this sets the virtio-scsi-device bus
     * name as before.
     */
    if (qdev->id) {
        bus_name = g_strdup_printf("%s.0", qdev->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_scsi_instance_init(Object *obj)
{
    VirtIOSCSICcw *dev = VIRTIO_SCSI_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SCSI);
}

static const Property virtio_ccw_scsi_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
};

static void virtio_ccw_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_scsi_realize;
    device_class_set_props(dc, virtio_ccw_scsi_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo virtio_ccw_scsi = {
    .name          = TYPE_VIRTIO_SCSI_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIOSCSICcw),
    .instance_init = virtio_ccw_scsi_instance_init,
    .class_init    = virtio_ccw_scsi_class_init,
};

static void virtio_ccw_scsi_register(void)
{
    type_register_static(&virtio_ccw_scsi);
}

type_init(virtio_ccw_scsi_register)
