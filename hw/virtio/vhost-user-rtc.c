/*
 * Vhost-user RTC virtio device
 *
 * Copyright (c) 2025 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
 * Copyright 2026 Panasonic Automotive Systems Co., Ltd.
 *
 * Simple wrapper of the generic vhost-user-device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/vhost-user-rtc.h"
#include "standard-headers/linux/virtio_ids.h"

static const VMStateDescription vu_rtc_vmstate = {
    .name = "vhost-user-rtc",
    .unmigratable = 1,
};

static const Property vrtc_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
};

static void vu_rtc_base_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubs = VHOST_USER_BASE_GET_CLASS(dev);

    vub->virtio_id = VIRTIO_ID_CLOCK;
    vub->num_vqs = 2;
    vub->config_size = 0;
    vub->vq_size = 1024;

    vubs->parent_realize(dev, errp);
}

static void vu_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);

    dc->vmsd = &vu_rtc_vmstate;
    device_class_set_props(dc, vrtc_properties);
    device_class_set_parent_realize(dc, vu_rtc_base_realize,
                                    &vubc->parent_realize);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vu_rtc_info = {
    .name = TYPE_VHOST_USER_RTC,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserRTC),
    .class_init = vu_rtc_class_init,
    .secure = true,
};

static void vu_rtc_register_types(void)
{
    type_register_static(&vu_rtc_info);
}

type_init(vu_rtc_register_types)
