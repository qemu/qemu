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

static const int feature_bits[] = {
    VIRTIO_I2C_F_ZERO_LENGTH_REQUEST,
    VIRTIO_F_RING_RESET,
    VHOST_INVALID_FEATURE_BIT
};

static void vu_i2c_start(VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);
    int ret, i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&i2c->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, i2c->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    i2c->vhost_dev.acked_features = vdev->guest_features;

    ret = vhost_dev_start(&i2c->vhost_dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost-user-i2c: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < i2c->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&i2c->vhost_dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, i2c->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&i2c->vhost_dev, vdev);
}

static void vu_i2c_stop(VirtIODevice *vdev)
{
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&i2c->vhost_dev, vdev, true);

    ret = k->set_guest_notifiers(qbus->parent, i2c->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&i2c->vhost_dev, vdev);
}

static void vu_i2c_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);
    bool should_start = virtio_device_should_start(vdev, status);

    if (vhost_dev_is_started(&i2c->vhost_dev) == should_start) {
        return;
    }

    if (should_start) {
        vu_i2c_start(vdev);
    } else {
        vu_i2c_stop(vdev);
    }
}

static uint64_t vu_i2c_get_features(VirtIODevice *vdev,
                                    uint64_t requested_features, Error **errp)
{
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);

    virtio_add_feature(&requested_features, VIRTIO_I2C_F_ZERO_LENGTH_REQUEST);
    return vhost_get_features(&i2c->vhost_dev, feature_bits, requested_features);
}

static void vu_i2c_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void vu_i2c_guest_notifier_mask(VirtIODevice *vdev, int idx, bool mask)
{
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);

    vhost_virtqueue_mask(&i2c->vhost_dev, vdev, idx, mask);
}

static bool vu_i2c_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);

    return vhost_virtqueue_pending(&i2c->vhost_dev, idx);
}

static void do_vhost_user_cleanup(VirtIODevice *vdev, VHostUserI2C *i2c)
{
    vhost_user_cleanup(&i2c->vhost_user);
    virtio_delete_queue(i2c->vq);
    virtio_cleanup(vdev);
}

static int vu_i2c_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);

    if (i2c->connected) {
        return 0;
    }
    i2c->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vu_i2c_start(vdev);
    }

    return 0;
}

static void vu_i2c_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);

    if (!i2c->connected) {
        return;
    }
    i2c->connected = false;

    if (vhost_dev_is_started(&i2c->vhost_dev)) {
        vu_i2c_stop(vdev);
    }
}

static void vu_i2c_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserI2C *i2c = VHOST_USER_I2C(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vu_i2c_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&i2c->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        vu_i2c_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vu_i2c_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserI2C *i2c = VHOST_USER_I2C(dev);
    int ret;

    if (!i2c->chardev.chr) {
        error_setg(errp, "vhost-user-i2c: missing chardev");
        return;
    }

    if (!vhost_user_init(&i2c->vhost_user, &i2c->chardev, errp)) {
        return;
    }

    virtio_init(vdev, VIRTIO_ID_I2C_ADAPTER, 0);

    i2c->vhost_dev.nvqs = 1;
    i2c->vq = virtio_add_queue(vdev, 4, vu_i2c_handle_output);
    i2c->vhost_dev.vqs = g_new0(struct vhost_virtqueue, i2c->vhost_dev.nvqs);

    ret = vhost_dev_init(&i2c->vhost_dev, &i2c->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, errp);
    if (ret < 0) {
        g_free(i2c->vhost_dev.vqs);
        do_vhost_user_cleanup(vdev, i2c);
    }

    qemu_chr_fe_set_handlers(&i2c->chardev, NULL, NULL, vu_i2c_event, NULL,
                             dev, NULL, true);
}

static void vu_i2c_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserI2C *i2c = VHOST_USER_I2C(dev);
    struct vhost_virtqueue *vhost_vqs = i2c->vhost_dev.vqs;

    /* This will stop vhost backend if appropriate. */
    vu_i2c_set_status(vdev, 0);
    vhost_dev_cleanup(&i2c->vhost_dev);
    g_free(vhost_vqs);
    do_vhost_user_cleanup(vdev, i2c);
}

static const VMStateDescription vu_i2c_vmstate = {
    .name = "vhost-user-i2c",
    .unmigratable = 1,
};

static Property vu_i2c_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserI2C, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vu_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vu_i2c_properties);
    dc->vmsd = &vu_i2c_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    vdc->realize = vu_i2c_device_realize;
    vdc->unrealize = vu_i2c_device_unrealize;
    vdc->get_features = vu_i2c_get_features;
    vdc->set_status = vu_i2c_set_status;
    vdc->guest_notifier_mask = vu_i2c_guest_notifier_mask;
    vdc->guest_notifier_pending = vu_i2c_guest_notifier_pending;
}

static const TypeInfo vu_i2c_info = {
    .name = TYPE_VHOST_USER_I2C,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserI2C),
    .class_init = vu_i2c_class_init,
};

static void vu_i2c_register_types(void)
{
    type_register_static(&vu_i2c_info);
}

type_init(vu_i2c_register_types)
