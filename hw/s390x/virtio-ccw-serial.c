/*
 * virtio ccw serial implementation
 *
 * Copyright 2012, 2015 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-serial.h"
#include "virtio-ccw.h"

#define TYPE_VIRTIO_SERIAL_CCW "virtio-serial-ccw"
OBJECT_DECLARE_SIMPLE_TYPE(VirtioSerialCcw, VIRTIO_SERIAL_CCW)

struct VirtioSerialCcw {
    VirtioCcwDevice parent_obj;
    VirtIOSerial vdev;
};

static void virtio_ccw_serial_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtioSerialCcw *dev = VIRTIO_SERIAL_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *proxy = DEVICE(ccw_dev);
    char *bus_name;

    /*
     * For command line compatibility, this sets the virtio-serial-device bus
     * name as before.
     */
    if (proxy->id) {
        bus_name = g_strdup_printf("%s.0", proxy->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}


static void virtio_ccw_serial_instance_init(Object *obj)
{
    VirtioSerialCcw *dev = VIRTIO_SERIAL_CCW(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SERIAL);
}

static Property virtio_ccw_serial_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_ccw_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_serial_realize;
    device_class_set_props(dc, virtio_ccw_serial_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo virtio_ccw_serial = {
    .name          = TYPE_VIRTIO_SERIAL_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtioSerialCcw),
    .instance_init = virtio_ccw_serial_instance_init,
    .class_init    = virtio_ccw_serial_class_init,
};

static void virtio_ccw_serial_register(void)
{
    type_register_static(&virtio_ccw_serial);
}

type_init(virtio_ccw_serial_register)
