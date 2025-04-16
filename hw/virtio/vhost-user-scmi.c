/*
 * Vhost-user SCMI virtio device
 *
 * SPDX-FileCopyrightText: Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implementation based on other vhost-user devices in QEMU.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vhost-user-scmi.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_scmi.h"
#include "trace.h"

/*
 * In this version, we don't support VIRTIO_SCMI_F_SHARED_MEMORY.
 * Note that VIRTIO_SCMI_F_SHARED_MEMORY is currently not supported in
 * Linux VirtIO SCMI guest driver.
 */
static const int feature_bits[] = {
    VIRTIO_F_VERSION_1,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_RING_RESET,
    VIRTIO_SCMI_F_P2A_CHANNELS,
    VHOST_INVALID_FEATURE_BIT
};

static int vu_scmi_start(VirtIODevice *vdev)
{
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    struct vhost_dev *vhost_dev = &scmi->vhost_dev;
    int ret, i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, vhost_dev->nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", ret);
        goto err_host_notifiers;
    }

    vhost_ack_features(vhost_dev, feature_bits, vdev->guest_features);

    ret = vhost_dev_start(vhost_dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost-user-scmi: %d", ret);
        goto err_guest_notifiers;
    }
    scmi->started_vu = true;

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < scmi->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(vhost_dev, vdev, i, false);
    }
    return 0;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vhost_dev->nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(vhost_dev, vdev);

    return ret;
}

static int vu_scmi_stop(VirtIODevice *vdev)
{
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    struct vhost_dev *vhost_dev = &scmi->vhost_dev;
    int ret;

    /* vhost_dev_is_started() check in the callers is not fully reliable. */
    if (!scmi->started_vu) {
        return 0;
    }
    scmi->started_vu = false;

    if (!k->set_guest_notifiers) {
        return 0;
    }

    ret = vhost_dev_stop(vhost_dev, vdev, true);

    if (k->set_guest_notifiers(qbus->parent, vhost_dev->nvqs, false) < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return -1;
    }
    vhost_dev_disable_notifiers(vhost_dev, vdev);
    return ret;
}

static int vu_scmi_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);
    bool should_start = virtio_device_should_start(vdev, status);

    if (!scmi->connected) {
        return -1;
    }
    if (vhost_dev_is_started(&scmi->vhost_dev) == should_start) {
        return 0;
    }

    if (should_start) {
        vu_scmi_start(vdev);
    } else {
        int ret;
        ret = vu_scmi_stop(vdev);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static uint64_t vu_scmi_get_features(VirtIODevice *vdev, uint64_t features,
                                     Error **errp)
{
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);

    return vhost_get_features(&scmi->vhost_dev, feature_bits, features);
}

static void vu_scmi_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void vu_scmi_guest_notifier_mask(VirtIODevice *vdev, int idx, bool mask)
{
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);

    if (idx == VIRTIO_CONFIG_IRQ_IDX) {
        return;
    }

    vhost_virtqueue_mask(&scmi->vhost_dev, vdev, idx, mask);
}

static bool vu_scmi_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);

    return vhost_virtqueue_pending(&scmi->vhost_dev, idx);
}

static void vu_scmi_connect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);

    if (scmi->connected) {
        return;
    }
    scmi->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        vu_scmi_start(vdev);
    }
}

static void vu_scmi_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCMI *scmi = VHOST_USER_SCMI(vdev);

    if (!scmi->connected) {
        return;
    }
    scmi->connected = false;

    if (vhost_dev_is_started(&scmi->vhost_dev)) {
        vu_scmi_stop(vdev);
    }
}

static void vu_scmi_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        vu_scmi_connect(dev);
        break;
    case CHR_EVENT_CLOSED:
        vu_scmi_disconnect(dev);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void do_vhost_user_cleanup(VirtIODevice *vdev, VHostUserSCMI *scmi)
{
    virtio_delete_queue(scmi->cmd_vq);
    virtio_delete_queue(scmi->event_vq);
    g_free(scmi->vhost_dev.vqs);
    virtio_cleanup(vdev);
    vhost_user_cleanup(&scmi->vhost_user);
}

static void vu_scmi_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCMI *scmi = VHOST_USER_SCMI(dev);
    int ret;

    if (!scmi->chardev.chr) {
        error_setg(errp, "vhost-user-scmi: chardev is mandatory");
        return;
    }

    vdev->host_features |= (1ULL << VIRTIO_SCMI_F_P2A_CHANNELS);

    if (!vhost_user_init(&scmi->vhost_user, &scmi->chardev, errp)) {
        return;
    }

    virtio_init(vdev, VIRTIO_ID_SCMI, 0);

    scmi->cmd_vq = virtio_add_queue(vdev, 256, vu_scmi_handle_output);
    scmi->event_vq = virtio_add_queue(vdev, 256, vu_scmi_handle_output);
    scmi->vhost_dev.nvqs = 2;
    scmi->vhost_dev.vqs = g_new0(struct vhost_virtqueue, scmi->vhost_dev.nvqs);

    ret = vhost_dev_init(&scmi->vhost_dev, &scmi->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, errp);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "vhost-user-scmi: vhost_dev_init() failed");
        do_vhost_user_cleanup(vdev, scmi);
        return;
    }

    qemu_chr_fe_set_handlers(&scmi->chardev, NULL, NULL, vu_scmi_event, NULL,
                             dev, NULL, true);
}

static void vu_scmi_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCMI *scmi = VHOST_USER_SCMI(dev);

    vu_scmi_set_status(vdev, 0);
    vhost_dev_cleanup(&scmi->vhost_dev);
    do_vhost_user_cleanup(vdev, scmi);
}

static const VMStateDescription vu_scmi_vmstate = {
    .name = "vhost-user-scmi",
    .unmigratable = 1,
};

static const Property vu_scmi_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserSCMI, chardev),
};

static void vu_scmi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vu_scmi_properties);
    dc->vmsd = &vu_scmi_vmstate;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    vdc->realize = vu_scmi_device_realize;
    vdc->unrealize = vu_scmi_device_unrealize;
    vdc->get_features = vu_scmi_get_features;
    vdc->set_status = vu_scmi_set_status;
    vdc->guest_notifier_mask = vu_scmi_guest_notifier_mask;
    vdc->guest_notifier_pending = vu_scmi_guest_notifier_pending;
}

static const TypeInfo vu_scmi_info = {
    .name = TYPE_VHOST_USER_SCMI,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserSCMI),
    .class_init = vu_scmi_class_init,
};

static void vu_scmi_register_types(void)
{
    type_register_static(&vu_scmi_info);
}

type_init(vu_scmi_register_types)
