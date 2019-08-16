/*
 * virtio ccw crypto implementation
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

static void virtio_ccw_crypto_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOCryptoCcw *dev = VIRTIO_CRYPTO_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    Error *err = NULL;

    qdev_set_parent_bus(vdev, BUS(&ccw_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_link(OBJECT(vdev),
                             OBJECT(dev->vdev.conf.cryptodev), "cryptodev",
                             NULL);
}

static void virtio_ccw_crypto_instance_init(Object *obj)
{
    VirtIOCryptoCcw *dev = VIRTIO_CRYPTO_CCW(obj);
    VirtioCcwDevice *ccw_dev = VIRTIO_CCW_DEVICE(obj);

    ccw_dev->force_revision_1 = true;
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_CRYPTO);
}

static Property virtio_ccw_crypto_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_ccw_crypto_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);

    k->realize = virtio_ccw_crypto_realize;
    dc->props = virtio_ccw_crypto_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo virtio_ccw_crypto = {
    .name          = TYPE_VIRTIO_CRYPTO_CCW,
    .parent        = TYPE_VIRTIO_CCW_DEVICE,
    .instance_size = sizeof(VirtIOCryptoCcw),
    .instance_init = virtio_ccw_crypto_instance_init,
    .class_init    = virtio_ccw_crypto_class_init,
};

static void virtio_ccw_crypto_register(void)
{
    type_register_static(&virtio_ccw_crypto);
}

type_init(virtio_ccw_crypto_register)
