/*
 * Vhost-user i2c virtio device
 *
 * Copyright (c) 2021 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-i2c.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/virtio_ids.h"

static Property vi2c_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vi2c_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_GET_CLASS(dev);

    /* Fixed for I2C */
    vub->virtio_id = VIRTIO_ID_I2C_ADAPTER;
    vub->num_vqs = 1;
    vub->vq_size = 4;

    vubc->parent_realize(dev, errp);
}

static const VMStateDescription vu_i2c_vmstate = {
    .name = "vhost-user-i2c",
    .unmigratable = 1,
};

static void vu_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);

    dc->vmsd = &vu_i2c_vmstate;
    device_class_set_props(dc, vi2c_properties);
    device_class_set_parent_realize(dc, vi2c_realize,
                                    &vubc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vu_i2c_info = {
    .name = TYPE_VHOST_USER_I2C,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserI2C),
    .class_init = vu_i2c_class_init,
};

static void vu_i2c_register_types(void)
{
    type_register_static(&vu_i2c_info);
}

type_init(vu_i2c_register_types)
