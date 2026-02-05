/*
 * Vhost-user spi virtio device
 *
 * Copyright (C) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-spi.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_spi.h"

static const Property vspi_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
};

static void vspi_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_GET_CLASS(dev);

    /* Fixed for SPI */
    vub->virtio_id = VIRTIO_ID_SPI;
    vub->num_vqs = 1;
    vub->vq_size = 4;
    vub->config_size = sizeof(struct virtio_spi_config);

    vubc->parent_realize(dev, errp);
}

static const VMStateDescription vu_spi_vmstate = {
    .name = "vhost-user-spi",
    .unmigratable = 1,
};

static void vu_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);

    dc->vmsd = &vu_spi_vmstate;
    device_class_set_props(dc, vspi_properties);
    device_class_set_parent_realize(dc, vspi_realize,
                                    &vubc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vu_spi_info = {
    .name = TYPE_VHOST_USER_SPI,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserSPI),
    .class_init = vu_spi_class_init,
};

static void vu_spi_register_types(void)
{
    type_register_static(&vu_spi_info);
}

type_init(vu_spi_register_types)
