/*
 * Vhost-user RNG virtio device
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * Implementation seriously tailored on vhost-user-i2c.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-rng.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/virtio_ids.h"

static const int feature_bits[] = {
    VIRTIO_F_RING_RESET,
    VHOST_INVALID_FEATURE_BIT
};

static void vu_rng_start(VirtIODevice *vdev)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;
    int i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&rng->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, rng->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    rng->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&rng->vhost_dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost-user-rng: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < rng->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&rng->vhost_dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, rng->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&rng->vhost_dev, vdev);
}

static void vu_rng_stop(VirtIODevice *vdev)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&rng->vhost_dev, vdev, true);

    ret = k->set_guest_notifiers(qbus->parent, rng->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&rng->vhost_dev, vdev);
}

static void vu_rng_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);
    bool should_start = virtio_device_should_start(vdev, status);

    if (vhost_dev_is_started(&rng->vhost_dev) == should_start) {
        return;
    }

    if (should_start) {
        vu_rng_start(vdev);
    } else {
        vu_rng_stop(vdev);
    }
}

static uint64_t vu_rng_get_features(VirtIODevice *vdev,
                                    uint64_t requested_features, Error **errp)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);

    return vhost_get_features(&rng->vhost_dev, feature_bits,
                              requested_features);
}

static void vu_rng_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void vu_rng_guest_notifier_mask(VirtIODevice *vdev, int idx, bool mask)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);

    vhost_virtqueue_mask(&rng->vhost_dev, vdev, idx, mask);
}

static bool vu_rng_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);

    return vhost_virtqueue_pending(&rng->vhost_dev, idx);
}

static void vu_rng_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);

    if (rng->connected) {
        return;
    }

    rng->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vu_rng_start(vdev);
    }
}

static void vu_rng_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);

    if (!rng->connected) {
        return;
    }

    rng->connected = false;

    if (vhost_dev_is_started(&rng->vhost_dev)) {
        vu_rng_stop(vdev);
    }
}

static void vu_rng_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        vu_rng_connect(dev);
        break;
    case CHR_EVENT_CLOSED:
        vu_rng_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vu_rng_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRNG *rng = VHOST_USER_RNG(dev);
    int ret;

    if (!rng->chardev.chr) {
        error_setg(errp, "missing chardev");
        return;
    }

    if (!vhost_user_init(&rng->vhost_user, &rng->chardev, errp)) {
        return;
    }

    virtio_init(vdev, VIRTIO_ID_RNG, 0);

    rng->req_vq = virtio_add_queue(vdev, 4, vu_rng_handle_output);
    if (!rng->req_vq) {
        error_setg_errno(errp, -1, "virtio_add_queue() failed");
        goto virtio_add_queue_failed;
    }

    rng->vhost_dev.nvqs = 1;
    rng->vhost_dev.vqs = g_new0(struct vhost_virtqueue, rng->vhost_dev.nvqs);
    ret = vhost_dev_init(&rng->vhost_dev, &rng->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, errp);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost_dev_init() failed");
        goto vhost_dev_init_failed;
    }

    qemu_chr_fe_set_handlers(&rng->chardev, NULL, NULL, vu_rng_event, NULL,
                             dev, NULL, true);

    return;

vhost_dev_init_failed:
    virtio_delete_queue(rng->req_vq);
virtio_add_queue_failed:
    virtio_cleanup(vdev);
    vhost_user_cleanup(&rng->vhost_user);
}

static void vu_rng_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserRNG *rng = VHOST_USER_RNG(dev);

    vu_rng_set_status(vdev, 0);

    vhost_dev_cleanup(&rng->vhost_dev);
    g_free(rng->vhost_dev.vqs);
    rng->vhost_dev.vqs = NULL;
    virtio_delete_queue(rng->req_vq);
    virtio_cleanup(vdev);
    vhost_user_cleanup(&rng->vhost_user);
}

static struct vhost_dev *vu_rng_get_vhost(VirtIODevice *vdev)
{
    VHostUserRNG *rng = VHOST_USER_RNG(vdev);
    return &rng->vhost_dev;
}

static const VMStateDescription vu_rng_vmstate = {
    .name = "vhost-user-rng",
    .unmigratable = 1,
};

static Property vu_rng_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserRNG, chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vu_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vu_rng_properties);
    dc->vmsd = &vu_rng_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    vdc->realize = vu_rng_device_realize;
    vdc->unrealize = vu_rng_device_unrealize;
    vdc->get_features = vu_rng_get_features;
    vdc->set_status = vu_rng_set_status;
    vdc->guest_notifier_mask = vu_rng_guest_notifier_mask;
    vdc->guest_notifier_pending = vu_rng_guest_notifier_pending;
    vdc->get_vhost = vu_rng_get_vhost;
}

static const TypeInfo vu_rng_info = {
    .name = TYPE_VHOST_USER_RNG,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserRNG),
    .class_init = vu_rng_class_init,
};

static void vu_rng_register_types(void)
{
    type_register_static(&vu_rng_info);
}

type_init(vu_rng_register_types)
