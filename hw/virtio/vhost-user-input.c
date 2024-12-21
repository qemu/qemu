/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-input.h"

static const Property vinput_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBase, chardev),
};

static void vinput_realize(DeviceState *dev, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    VHostUserBaseClass *vubc = VHOST_USER_BASE_GET_CLASS(dev);

    /* Fixed for input device */
    vub->virtio_id = VIRTIO_ID_INPUT;
    vub->num_vqs = 2;
    vub->vq_size = 4;
    vub->config_size = sizeof(virtio_input_config);

    vubc->parent_realize(dev, errp);
}

static const VMStateDescription vmstate_vhost_input = {
    .name = "vhost-user-input",
    .unmigratable = 1,
};

static void vhost_input_class_init(ObjectClass *klass, void *data)
{
    VHostUserBaseClass *vubc = VHOST_USER_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vhost_input;
    device_class_set_props(dc, vinput_properties);
    device_class_set_parent_realize(dc, vinput_realize,
                                    &vubc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vhost_input_info = {
    .name          = TYPE_VHOST_USER_INPUT,
    .parent        = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserInput),
    .class_init    = vhost_input_class_init,
};

static void vhost_input_register_types(void)
{
    type_register_static(&vhost_input_info);
}

type_init(vhost_input_register_types)
