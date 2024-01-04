/*
 * Vhost-user snd virtio device
 *
 * Copyright (c) 2023 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
 *
 * Simple wrapper of the generic vhost-user-device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-snd.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_snd.h"

static const VMStateDescription vu_snd_vmstate = {
    .name = "vhost-user-snd",
    .unmigratable = 1,
};

static Property vsnd_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vu_snd_base_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubs = VHOST_USER_BASE_GET_CLASS(dev);

    vub->virtio_id = VIRTIO_ID_SOUND;
    vub->num_vqs = 4;
    vub->config_size = sizeof(struct virtio_snd_config);
    vub->vq_size = 64;

    vubs->parent_realize(dev, errp);
}

static void vu_snd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);

    dc->vmsd = &vu_snd_vmstate;
    device_class_set_props(dc, vsnd_properties);
    device_class_set_parent_realize(dc, vu_snd_base_realize,
                                    &vubc->parent_realize);

    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
}

static const TypeInfo vu_snd_info = {
    .name = TYPE_VHOST_USER_SND,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserSound),
    .class_init = vu_snd_class_init,
};

static void vu_snd_register_types(void)
{
    type_register_static(&vu_snd_info);
}

type_init(vu_snd_register_types)
