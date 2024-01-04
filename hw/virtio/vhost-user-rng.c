/*
 * Vhost-user RNG virtio device
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * Simple wrapper of the generic vhost-user-device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-rng.h"
#include "standard-headers/linux/virtio_ids.h"

static const VMStateDescription vu_rng_vmstate = {
    .name = "vhost-user-rng",
    .unmigratable = 1,
};

static Property vrng_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vu_rng_base_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubs = VHOST_USER_BASE_GET_CLASS(dev);

    /* Fixed for RNG */
    vub->virtio_id = VIRTIO_ID_RNG;
    vub->num_vqs = 1;
    vub->vq_size = 4;

    vubs->parent_realize(dev, errp);
}

static void vu_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);

    dc->vmsd = &vu_rng_vmstate;
    device_class_set_props(dc, vrng_properties);
    device_class_set_parent_realize(dc, vu_rng_base_realize,
                                    &vubc->parent_realize);

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vu_rng_info = {
    .name = TYPE_VHOST_USER_RNG,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserRNG),
    .class_init = vu_rng_class_init,
};

static void vu_rng_register_types(void)
{
    type_register_static(&vu_rng_info);
}

type_init(vu_rng_register_types)
