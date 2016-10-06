/*
 * Virtio vsock device
 *
 * Copyright 2015 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_vsock.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "hw/virtio/vhost-vsock.h"
#include "qemu/iov.h"
#include "monitor/monitor.h"

enum {
    VHOST_VSOCK_SAVEVM_VERSION = 0,

    VHOST_VSOCK_QUEUE_SIZE = 128,
};

static void vhost_vsock_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);
    struct virtio_vsock_config vsockcfg = {};

    virtio_stq_p(vdev, &vsockcfg.guest_cid, vsock->conf.guest_cid);
    memcpy(config, &vsockcfg, sizeof(vsockcfg));
}

static int vhost_vsock_set_guest_cid(VHostVSock *vsock)
{
    const VhostOps *vhost_ops = vsock->vhost_dev.vhost_ops;
    int ret;

    if (!vhost_ops->vhost_vsock_set_guest_cid) {
        return -ENOSYS;
    }

    ret = vhost_ops->vhost_vsock_set_guest_cid(&vsock->vhost_dev,
                                               vsock->conf.guest_cid);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

static int vhost_vsock_set_running(VHostVSock *vsock, int start)
{
    const VhostOps *vhost_ops = vsock->vhost_dev.vhost_ops;
    int ret;

    if (!vhost_ops->vhost_vsock_set_running) {
        return -ENOSYS;
    }

    ret = vhost_ops->vhost_vsock_set_running(&vsock->vhost_dev, start);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

static void vhost_vsock_start(VirtIODevice *vdev)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;
    int i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&vsock->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, vsock->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    vsock->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&vsock->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    ret = vhost_vsock_set_running(vsock, 1);
    if (ret < 0) {
        error_report("Error starting vhost vsock: %d", -ret);
        goto err_dev_start;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < vsock->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&vsock->vhost_dev, vdev, i, false);
    }

    return;

err_dev_start:
    vhost_dev_stop(&vsock->vhost_dev, vdev);
err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vsock->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&vsock->vhost_dev, vdev);
}

static void vhost_vsock_stop(VirtIODevice *vdev)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    ret = vhost_vsock_set_running(vsock, 0);
    if (ret < 0) {
        error_report("vhost vsock set running failed: %d", ret);
        return;
    }

    vhost_dev_stop(&vsock->vhost_dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, vsock->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&vsock->vhost_dev, vdev);
}

static void vhost_vsock_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (vsock->vhost_dev.started == should_start) {
        return;
    }

    if (should_start) {
        vhost_vsock_start(vdev);
    } else {
        vhost_vsock_stop(vdev);
    }
}

static uint64_t vhost_vsock_get_features(VirtIODevice *vdev,
                                         uint64_t requested_features,
                                         Error **errp)
{
    /* No feature bits used yet */
    return requested_features;
}

static void vhost_vsock_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /* Do nothing */
}

static void vhost_vsock_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                            bool mask)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);

    vhost_virtqueue_mask(&vsock->vhost_dev, vdev, idx, mask);
}

static bool vhost_vsock_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);

    return vhost_virtqueue_pending(&vsock->vhost_dev, idx);
}

static void vhost_vsock_send_transport_reset(VHostVSock *vsock)
{
    VirtQueueElement *elem;
    VirtQueue *vq = vsock->event_vq;
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
    virtio_notify(VIRTIO_DEVICE(vsock), vq);

out:
    g_free(elem);
}

static void vhost_vsock_post_load_timer_cleanup(VHostVSock *vsock)
{
    if (!vsock->post_load_timer) {
        return;
    }

    timer_del(vsock->post_load_timer);
    timer_free(vsock->post_load_timer);
    vsock->post_load_timer = NULL;
}

static void vhost_vsock_post_load_timer_cb(void *opaque)
{
    VHostVSock *vsock = opaque;

    vhost_vsock_post_load_timer_cleanup(vsock);
    vhost_vsock_send_transport_reset(vsock);
}

static void vhost_vsock_pre_save(void *opaque)
{
    VHostVSock *vsock = opaque;

    /* At this point, backend must be stopped, otherwise
     * it might keep writing to memory. */
    assert(!vsock->vhost_dev.started);
}

static int vhost_vsock_post_load(void *opaque, int version_id)
{
    VHostVSock *vsock = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(vsock);

    if (virtio_queue_get_addr(vdev, 2)) {
        /* Defer transport reset event to a vm clock timer so that virtqueue
         * changes happen after migration has completed.
         */
        assert(!vsock->post_load_timer);
        vsock->post_load_timer =
            timer_new_ns(QEMU_CLOCK_VIRTUAL,
                         vhost_vsock_post_load_timer_cb,
                         vsock);
        timer_mod(vsock->post_load_timer, 1);
    }
    return 0;
}

static const VMStateDescription vmstate_virtio_vhost_vsock = {
    .name = "virtio-vhost_vsock",
    .minimum_version_id = VHOST_VSOCK_SAVEVM_VERSION,
    .version_id = VHOST_VSOCK_SAVEVM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
    .pre_save = vhost_vsock_pre_save,
    .post_load = vhost_vsock_post_load,
};

static void vhost_vsock_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostVSock *vsock = VHOST_VSOCK(dev);
    int vhostfd;
    int ret;

    /* Refuse to use reserved CID numbers */
    if (vsock->conf.guest_cid <= 2) {
        error_setg(errp, "guest-cid property must be greater than 2");
        return;
    }

    if (vsock->conf.guest_cid > UINT32_MAX) {
        error_setg(errp, "guest-cid property must be a 32-bit number");
        return;
    }

    if (vsock->conf.vhostfd) {
        vhostfd = monitor_fd_param(cur_mon, vsock->conf.vhostfd, errp);
        if (vhostfd == -1) {
            error_prepend(errp, "vhost-vsock: unable to parse vhostfd: ");
            return;
        }
    } else {
        vhostfd = open("/dev/vhost-vsock", O_RDWR);
        if (vhostfd < 0) {
            error_setg_errno(errp, -errno,
                             "vhost-vsock: failed to open vhost device");
            return;
        }
    }

    virtio_init(vdev, "vhost-vsock", VIRTIO_ID_VSOCK,
                sizeof(struct virtio_vsock_config));

    /* Receive and transmit queues belong to vhost */
    virtio_add_queue(vdev, VHOST_VSOCK_QUEUE_SIZE, vhost_vsock_handle_output);
    virtio_add_queue(vdev, VHOST_VSOCK_QUEUE_SIZE, vhost_vsock_handle_output);

    /* The event queue belongs to QEMU */
    vsock->event_vq = virtio_add_queue(vdev, VHOST_VSOCK_QUEUE_SIZE,
                                       vhost_vsock_handle_output);

    vsock->vhost_dev.nvqs = ARRAY_SIZE(vsock->vhost_vqs);
    vsock->vhost_dev.vqs = vsock->vhost_vqs;
    ret = vhost_dev_init(&vsock->vhost_dev, (void *)(uintptr_t)vhostfd,
                         VHOST_BACKEND_TYPE_KERNEL, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost-vsock: vhost_dev_init failed");
        goto err_virtio;
    }

    ret = vhost_vsock_set_guest_cid(vsock);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost-vsock: unable to set guest cid");
        goto err_vhost_dev;
    }

    vsock->post_load_timer = NULL;
    return;

err_vhost_dev:
    vhost_dev_cleanup(&vsock->vhost_dev);
err_virtio:
    virtio_cleanup(vdev);
    close(vhostfd);
    return;
}

static void vhost_vsock_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostVSock *vsock = VHOST_VSOCK(dev);

    vhost_vsock_post_load_timer_cleanup(vsock);

    /* This will stop vhost backend if appropriate. */
    vhost_vsock_set_status(vdev, 0);

    vhost_dev_cleanup(&vsock->vhost_dev);
    virtio_cleanup(vdev);
}

static Property vhost_vsock_properties[] = {
    DEFINE_PROP_UINT64("guest-cid", VHostVSock, conf.guest_cid, 0),
    DEFINE_PROP_STRING("vhostfd", VHostVSock, conf.vhostfd),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_vsock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_vsock_properties;
    dc->vmsd = &vmstate_virtio_vhost_vsock;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = vhost_vsock_device_realize;
    vdc->unrealize = vhost_vsock_device_unrealize;
    vdc->get_features = vhost_vsock_get_features;
    vdc->get_config = vhost_vsock_get_config;
    vdc->set_status = vhost_vsock_set_status;
    vdc->guest_notifier_mask = vhost_vsock_guest_notifier_mask;
    vdc->guest_notifier_pending = vhost_vsock_guest_notifier_pending;
}

static const TypeInfo vhost_vsock_info = {
    .name = TYPE_VHOST_VSOCK,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostVSock),
    .class_init = vhost_vsock_class_init,
};

static void vhost_vsock_register_types(void)
{
    type_register_static(&vhost_vsock_info);
}

type_init(vhost_vsock_register_types)
