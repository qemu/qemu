/*
 * Parent class for vhost-vsock devices
 *
 * Copyright 2015-2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_vsock.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-vsock.h"
#include "qemu/iov.h"
#include "monitor/monitor.h"

int vhost_vsock_common_start(VirtIODevice *vdev)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;
    int i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&vvc->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, vvc->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    vvc->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&vvc->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < vvc->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&vvc->vhost_dev, vdev, i, false);
    }

    return 0;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vvc->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&vvc->vhost_dev, vdev);
    return ret;
}

void vhost_vsock_common_stop(VirtIODevice *vdev)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&vvc->vhost_dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, vvc->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&vvc->vhost_dev, vdev);
}


static void vhost_vsock_common_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /* Do nothing */
}

static void vhost_vsock_common_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                            bool mask)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);

    vhost_virtqueue_mask(&vvc->vhost_dev, vdev, idx, mask);
}

static bool vhost_vsock_common_guest_notifier_pending(VirtIODevice *vdev,
                                               int idx)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);

    return vhost_virtqueue_pending(&vvc->vhost_dev, idx);
}

static void vhost_vsock_common_send_transport_reset(VHostVSockCommon *vvc)
{
    VirtQueueElement *elem;
    VirtQueue *vq = vvc->event_vq;
    struct virtio_vsock_event event = {
        .id = cpu_to_le32(VIRTIO_VSOCK_EVENT_TRANSPORT_RESET),
    };

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        error_report("vhost-vsock missed transport reset event");
        return;
    }

    if (elem->out_num) {
        error_report("invalid vhost-vsock event virtqueue element with "
                     "out buffers");
        goto out;
    }

    if (iov_from_buf(elem->in_sg, elem->in_num, 0,
                     &event, sizeof(event)) != sizeof(event)) {
        error_report("vhost-vsock event virtqueue element is too short");
        goto out;
    }

    virtqueue_push(vq, elem, sizeof(event));
    virtio_notify(VIRTIO_DEVICE(vvc), vq);

out:
    g_free(elem);
}

static void vhost_vsock_common_post_load_timer_cleanup(VHostVSockCommon *vvc)
{
    if (!vvc->post_load_timer) {
        return;
    }

    timer_free(vvc->post_load_timer);
    vvc->post_load_timer = NULL;
}

static void vhost_vsock_common_post_load_timer_cb(void *opaque)
{
    VHostVSockCommon *vvc = opaque;

    vhost_vsock_common_post_load_timer_cleanup(vvc);
    vhost_vsock_common_send_transport_reset(vvc);
}

int vhost_vsock_common_pre_save(void *opaque)
{
    VHostVSockCommon *vvc = opaque;

    /*
     * At this point, backend must be stopped, otherwise
     * it might keep writing to memory.
     */
    assert(!vvc->vhost_dev.started);

    return 0;
}

int vhost_vsock_common_post_load(void *opaque, int version_id)
{
    VHostVSockCommon *vvc = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(vvc);

    if (virtio_queue_get_addr(vdev, 2)) {
        /*
         * Defer transport reset event to a vm clock timer so that virtqueue
         * changes happen after migration has completed.
         */
        assert(!vvc->post_load_timer);
        vvc->post_load_timer =
            timer_new_ns(QEMU_CLOCK_VIRTUAL,
                         vhost_vsock_common_post_load_timer_cb,
                         vvc);
        timer_mod(vvc->post_load_timer, 1);
    }
    return 0;
}

void vhost_vsock_common_realize(VirtIODevice *vdev, const char *name)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);

    virtio_init(vdev, name, VIRTIO_ID_VSOCK,
                sizeof(struct virtio_vsock_config));

    /* Receive and transmit queues belong to vhost */
    vvc->recv_vq = virtio_add_queue(vdev, VHOST_VSOCK_QUEUE_SIZE,
                                      vhost_vsock_common_handle_output);
    vvc->trans_vq = virtio_add_queue(vdev, VHOST_VSOCK_QUEUE_SIZE,
                                       vhost_vsock_common_handle_output);

    /* The event queue belongs to QEMU */
    vvc->event_vq = virtio_add_queue(vdev, VHOST_VSOCK_QUEUE_SIZE,
                                       vhost_vsock_common_handle_output);

    vvc->vhost_dev.nvqs = ARRAY_SIZE(vvc->vhost_vqs);
    vvc->vhost_dev.vqs = vvc->vhost_vqs;

    vvc->post_load_timer = NULL;
}

void vhost_vsock_common_unrealize(VirtIODevice *vdev)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);

    vhost_vsock_common_post_load_timer_cleanup(vvc);

    virtio_delete_queue(vvc->recv_vq);
    virtio_delete_queue(vvc->trans_vq);
    virtio_delete_queue(vvc->event_vq);
    virtio_cleanup(vdev);
}

static void vhost_vsock_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->guest_notifier_mask = vhost_vsock_common_guest_notifier_mask;
    vdc->guest_notifier_pending = vhost_vsock_common_guest_notifier_pending;
}

static const TypeInfo vhost_vsock_common_info = {
    .name = TYPE_VHOST_VSOCK_COMMON,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostVSockCommon),
    .class_init = vhost_vsock_common_class_init,
    .abstract = true,
};

static void vhost_vsock_common_register_types(void)
{
    type_register_static(&vhost_vsock_common_info);
}

type_init(vhost_vsock_common_register_types)
