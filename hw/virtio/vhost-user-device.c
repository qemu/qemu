/*
 * Generic vhost-user-device implementation for any vhost-user-backend
 *
 * This is a concrete implementation of vhost-user-base which can be
 * configured via properties. It is useful for development and
 * prototyping. It expects configuration details (if any) to be
 * handled by the vhost-user daemon itself.
 *
 * Copyright (c) 2023 Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-base.h"
#include "qemu/error-report.h"

/*
 * The following is a concrete implementation of the base class which
 * allows the user to define the key parameters via the command line.
 */

static const VMStateDescription vud_vmstate = {
    .name = "vhost-user-device",
    .unmigratable = 1,
};

static Property vud_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
    DEFINE_PROP_UINT16("virtio-id", VHostUserBase, virtio_id, 0),
    DEFINE_PROP_UINT32("vq_size", VHostUserBase, vq_size, 64),
    DEFINE_PROP_UINT32("num_vqs", VHostUserBase, num_vqs, 1),
    DEFINE_PROP_UINT32("config_size", VHostUserBase, config_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void vud_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* Reason: stop inexperienced users confusing themselves */
    dc->user_creatable = false;

    device_class_set_props(dc, vud_properties);
    dc->vmsd = &vud_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vud_info = {
    .name = TYPE_VHOST_USER_DEVICE,
    .parent = TYPE_VHOST_USER_BASE,
    .class_init = vud_class_init,
};

static void vu_register_types(void)
{
    type_register_static(&vud_info);
}

type_init(vu_register_types)
