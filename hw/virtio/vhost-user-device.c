/*
 * Generic vhost-user stub. This can be used to connect to any
 * vhost-user backend. All configuration details must be handled by
 * the vhost-user daemon itself
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
#include "hw/virtio/vhost-user-device.h"
#include "qemu/error-report.h"

static void vub_start(VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    VHostUserBase *vub = VHOST_USER_BASE(vdev);
    int ret, i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&vub->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, vub->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    vub->vhost_dev.acked_features = vdev->guest_features;

    ret = vhost_dev_start(&vub->vhost_dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost-user-device: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < vub->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&vub->vhost_dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vub->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&vub->vhost_dev, vdev);
}

static void vub_stop(VirtIODevice *vdev)
{
    VHostUserBase *vub = VHOST_USER_BASE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&vub->vhost_dev, vdev, true);

    ret = k->set_guest_notifiers(qbus->parent, vub->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&vub->vhost_dev, vdev);
}

static void vub_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserBase *vub = VHOST_USER_BASE(vdev);
    bool should_start = virtio_device_should_start(vdev, status);

    if (vhost_dev_is_started(&vub->vhost_dev) == should_start) {
        return;
    }

    if (should_start) {
        vub_start(vdev);
    } else {
        vub_stop(vdev);
    }
}

/*
 * For an implementation where everything is delegated to the backend
 * we don't do anything other than return the full feature set offered
 * by the daemon (module the reserved feature bit).
 */
static uint64_t vub_get_features(VirtIODevice *vdev,
                                 uint64_t requested_features, Error **errp)
{
    VHostUserBase *vub = VHOST_USER_BASE(vdev);
    /* This should be set when the vhost connection initialises */
    g_assert(vub->vhost_dev.features);
    return vub->vhost_dev.features & ~(1ULL << VHOST_USER_F_PROTOCOL_FEATURES);
}

/*
 * To handle VirtIO config we need to know the size of the config
 * space. We don't cache the config but re-fetch it from the guest
 * every time in case something has changed.
 */
static void vub_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserBase *vub = VHOST_USER_BASE(vdev);
    Error *local_err = NULL;

    /*
     * There will have been a warning during vhost_dev_init, but lets
     * assert here as nothing will go right now.
     */
    g_assert(vub->config_size && vub->vhost_user.supports_config == true);

    if (vhost_dev_get_config(&vub->vhost_dev, config,
                             vub->config_size, &local_err)) {
        error_report_err(local_err);
    }
}

/*
 * When the daemon signals an update to the config we just need to
 * signal the guest as we re-read the config on demand above.
 */
static int vub_config_notifier(struct vhost_dev *dev)
{
    virtio_notify_config(dev->vdev);
    return 0;
}

const VhostDevConfigOps vub_config_ops = {
    .vhost_dev_config_notifier = vub_config_notifier,
};

static void vub_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void do_vhost_user_cleanup(VirtIODevice *vdev, VHostUserBase *vub)
{
    vhost_user_cleanup(&vub->vhost_user);

    for (int i = 0; i < vub->num_vqs; i++) {
        VirtQueue *vq = g_ptr_array_index(vub->vqs, i);
        virtio_delete_queue(vq);
    }

    virtio_cleanup(vdev);
}

static int vub_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBase *vub = VHOST_USER_BASE(vdev);
    struct vhost_dev *vhost_dev = &vub->vhost_dev;

    if (vub->connected) {
        return 0;
    }
    vub->connected = true;

    /*
     * If we support VHOST_USER_GET_CONFIG we must enable the notifier
     * so we can ping the guest when it updates.
     */
    if (vub->vhost_user.supports_config) {
        vhost_dev_set_config_notifier(vhost_dev, &vub_config_ops);
    }

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vub_start(vdev);
    }

    return 0;
}

static void vub_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBase *vub = VHOST_USER_BASE(vdev);

    if (!vub->connected) {
        return;
    }
    vub->connected = false;

    if (vhost_dev_is_started(&vub->vhost_dev)) {
        vub_stop(vdev);
    }
}

static void vub_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBase *vub = VHOST_USER_BASE(vdev);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vub_connect(dev) < 0) {
            qemu_chr_fe_disconnect(&vub->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        vub_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void vub_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    int ret;

    if (!vub->chardev.chr) {
        error_setg(errp, "vhost-user-device: missing chardev");
        return;
    }

    if (!vub->virtio_id) {
        error_setg(errp, "vhost-user-device: need to define device id");
        return;
    }

    if (!vub->num_vqs) {
        vub->num_vqs = 1; /* reasonable default? */
    }

    /*
     * We can't handle config requests unless we know the size of the
     * config region, specialisations of the vhost-user-device will be
     * able to set this.
     */
    if (vub->config_size) {
        vub->vhost_user.supports_config = true;
    }

    if (!vhost_user_init(&vub->vhost_user, &vub->chardev, errp)) {
        return;
    }

    virtio_init(vdev, vub->virtio_id, vub->config_size);

    /*
     * Disable guest notifiers, by default all notifications will be via the
     * asynchronous vhost-user socket.
     */
    vdev->use_guest_notifier_mask = false;

    /* Allocate queues */
    vub->vqs = g_ptr_array_sized_new(vub->num_vqs);
    for (int i = 0; i < vub->num_vqs; i++) {
        g_ptr_array_add(vub->vqs,
                        virtio_add_queue(vdev, 4, vub_handle_output));
    }

    vub->vhost_dev.nvqs = vub->num_vqs;
    vub->vhost_dev.vqs = g_new0(struct vhost_virtqueue, vub->vhost_dev.nvqs);

    /* connect to backend */
    ret = vhost_dev_init(&vub->vhost_dev, &vub->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, errp);

    if (ret < 0) {
        do_vhost_user_cleanup(vdev, vub);
    }

    qemu_chr_fe_set_handlers(&vub->chardev, NULL, NULL, vub_event, NULL,
                             dev, NULL, true);
}

static void vub_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBase *vub = VHOST_USER_BASE(dev);
    struct vhost_virtqueue *vhost_vqs = vub->vhost_dev.vqs;

    /* This will stop vhost backend if appropriate. */
    vub_set_status(vdev, 0);
    vhost_dev_cleanup(&vub->vhost_dev);
    g_free(vhost_vqs);
    do_vhost_user_cleanup(vdev, vub);
}

static void vub_class_init(ObjectClass *klass, void *data)
{
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    vdc->realize = vub_device_realize;
    vdc->unrealize = vub_device_unrealize;
    vdc->get_features = vub_get_features;
    vdc->get_config = vub_get_config;
    vdc->set_status = vub_set_status;
}

static const TypeInfo vub_info = {
    .name = TYPE_VHOST_USER_BASE,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserBase),
    .class_init = vub_class_init,
    .class_size = sizeof(VHostUserBaseClass),
    .abstract = true
};


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
    DEFINE_PROP_UINT32("num_vqs", VHostUserBase, num_vqs, 1),
    DEFINE_PROP_UINT32("config_size", VHostUserBase, config_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void vud_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, vud_properties);
    dc->vmsd = &vud_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vud_info = {
    .name = TYPE_VHOST_USER_DEVICE,
    .parent = TYPE_VHOST_USER_BASE,
    .instance_size = sizeof(VHostUserBase),
    .class_init = vud_class_init,
    .class_size = sizeof(VHostUserBaseClass),
};

static void vu_register_types(void)
{
    type_register_static(&vub_info);
    type_register_static(&vud_info);
}

type_init(vu_register_types)
