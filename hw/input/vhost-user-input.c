/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu-common.h"

#include "hw/qdev.h"
#include "hw/virtio/virtio-input.h"

static int vhost_input_config_change(struct vhost_dev *dev)
{
    error_report("vhost-user-input: unhandled backend config change");
    return -1;
}

static const VhostDevConfigOps config_ops = {
    .vhost_dev_config_notifier = vhost_input_config_change,
};

static void vhost_input_realize(DeviceState *dev, Error **errp)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(dev);
    VirtIOInput *vinput = VIRTIO_INPUT(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    vhost_dev_set_config_notifier(&vhi->vhost->dev, &config_ops);
    vinput->cfg_size = sizeof_field(virtio_input_config, u);
    if (vhost_user_backend_dev_init(vhi->vhost, vdev, 2, errp) == -1) {
        return;
    }
}

static void vhost_input_change_active(VirtIOInput *vinput)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(vinput);

    if (vinput->active) {
        vhost_user_backend_start(vhi->vhost);
    } else {
        vhost_user_backend_stop(vhi->vhost);
    }
}

static void vhost_input_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOInput *vinput = VIRTIO_INPUT(vdev);
    VHostUserInput *vhi = VHOST_USER_INPUT(vdev);
    int ret;

    memset(config_data, 0, vinput->cfg_size);

    ret = vhost_dev_get_config(&vhi->vhost->dev, config_data, vinput->cfg_size);
    if (ret) {
        error_report("vhost-user-input: get device config space failed");
        return;
    }
}

static void vhost_input_set_config(VirtIODevice *vdev,
                                   const uint8_t *config_data)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(vdev);
    int ret;

    ret = vhost_dev_set_config(&vhi->vhost->dev, config_data,
                               0, sizeof(virtio_input_config),
                               VHOST_SET_CONFIG_TYPE_MASTER);
    if (ret) {
        error_report("vhost-user-input: set device config space failed");
        return;
    }

    virtio_notify_config(vdev);
}

static const VMStateDescription vmstate_vhost_input = {
    .name = "vhost-user-input",
    .unmigratable = 1,
};

static void vhost_input_class_init(ObjectClass *klass, void *data)
{
    VirtIOInputClass *vic = VIRTIO_INPUT_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vhost_input;
    vdc->get_config = vhost_input_get_config;
    vdc->set_config = vhost_input_set_config;
    vic->realize = vhost_input_realize;
    vic->change_active = vhost_input_change_active;
}

static void vhost_input_init(Object *obj)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(obj);

    vhi->vhost = VHOST_USER_BACKEND(object_new(TYPE_VHOST_USER_BACKEND));
    object_property_add_alias(obj, "chardev",
                              OBJECT(vhi->vhost), "chardev", &error_abort);
}

static void vhost_input_finalize(Object *obj)
{
    VHostUserInput *vhi = VHOST_USER_INPUT(obj);

    object_unref(OBJECT(vhi->vhost));
}

static const TypeInfo vhost_input_info = {
    .name          = TYPE_VHOST_USER_INPUT,
    .parent        = TYPE_VIRTIO_INPUT,
    .instance_size = sizeof(VHostUserInput),
    .instance_init = vhost_input_init,
    .instance_finalize = vhost_input_finalize,
    .class_init    = vhost_input_class_init,
};

static void vhost_input_register_types(void)
{
    type_register_static(&vhost_input_info);
}

type_init(vhost_input_register_types)
