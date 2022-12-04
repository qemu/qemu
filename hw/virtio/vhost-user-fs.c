/*
 * Vhost-user filesystem virtio device
 *
 * Copyright 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include "standard-headers/linux/virtio_fs.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/error-report.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user-fs.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"

static const int user_feature_bits[] = {
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_F_RING_PACKED,
    VIRTIO_F_IOMMU_PLATFORM,
    VIRTIO_F_RING_RESET,

    VHOST_INVALID_FEATURE_BIT
};

static void vuf_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    struct virtio_fs_config fscfg = {};

    memcpy((char *)fscfg.tag, fs->conf.tag,
           MIN(strlen(fs->conf.tag) + 1, sizeof(fscfg.tag)));

    virtio_stl_p(vdev, &fscfg.num_request_queues, fs->conf.num_request_queues);

    memcpy(config, &fscfg, sizeof(fscfg));
}

static void vuf_start(VirtIODevice *vdev)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;
    int i;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&fs->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, fs->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    fs->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&fs->vhost_dev, vdev, true);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < fs->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&fs->vhost_dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, fs->vhost_dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&fs->vhost_dev, vdev);
}

static void vuf_stop(VirtIODevice *vdev)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&fs->vhost_dev, vdev, true);

    ret = k->set_guest_notifiers(qbus->parent, fs->vhost_dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&fs->vhost_dev, vdev);
}

static void vuf_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    bool should_start = virtio_device_should_start(vdev, status);

    if (vhost_dev_is_started(&fs->vhost_dev) == should_start) {
        return;
    }

    if (should_start) {
        vuf_start(vdev);
    } else {
        vuf_stop(vdev);
    }
}

static uint64_t vuf_get_features(VirtIODevice *vdev,
                                 uint64_t features,
                                 Error **errp)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    return vhost_get_features(&fs->vhost_dev, user_feature_bits, features);
}

static void vuf_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /*
     * Not normally called; it's the daemon that handles the queue;
     * however virtio's cleanup path can call this.
     */
}

static void vuf_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                            bool mask)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    vhost_virtqueue_mask(&fs->vhost_dev, vdev, idx, mask);
}

static bool vuf_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);

    return vhost_virtqueue_pending(&fs->vhost_dev, idx);
}

static void vuf_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(dev);
    unsigned int i;
    size_t len;
    int ret;

    if (!fs->conf.chardev.chr) {
        error_setg(errp, "missing chardev");
        return;
    }

    if (!fs->conf.tag) {
        error_setg(errp, "missing tag property");
        return;
    }
    len = strlen(fs->conf.tag);
    if (len == 0) {
        error_setg(errp, "tag property cannot be empty");
        return;
    }
    if (len > sizeof_field(struct virtio_fs_config, tag)) {
        error_setg(errp, "tag property must be %zu bytes or less",
                   sizeof_field(struct virtio_fs_config, tag));
        return;
    }

    if (fs->conf.num_request_queues == 0) {
        error_setg(errp, "num-request-queues property must be larger than 0");
        return;
    }

    if (!is_power_of_2(fs->conf.queue_size)) {
        error_setg(errp, "queue-size property must be a power of 2");
        return;
    }

    if (fs->conf.queue_size > VIRTQUEUE_MAX_SIZE) {
        error_setg(errp, "queue-size property must be %u or smaller",
                   VIRTQUEUE_MAX_SIZE);
        return;
    }

    if (!vhost_user_init(&fs->vhost_user, &fs->conf.chardev, errp)) {
        return;
    }

    virtio_init(vdev, VIRTIO_ID_FS, sizeof(struct virtio_fs_config));

    /* Hiprio queue */
    fs->hiprio_vq = virtio_add_queue(vdev, fs->conf.queue_size, vuf_handle_output);

    /* Request queues */
    fs->req_vqs = g_new(VirtQueue *, fs->conf.num_request_queues);
    for (i = 0; i < fs->conf.num_request_queues; i++) {
        fs->req_vqs[i] = virtio_add_queue(vdev, fs->conf.queue_size, vuf_handle_output);
    }

    /* 1 high prio queue, plus the number configured */
    fs->vhost_dev.nvqs = 1 + fs->conf.num_request_queues;
    fs->vhost_dev.vqs = g_new0(struct vhost_virtqueue, fs->vhost_dev.nvqs);
    ret = vhost_dev_init(&fs->vhost_dev, &fs->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0, errp);
    if (ret < 0) {
        goto err_virtio;
    }

    return;

err_virtio:
    vhost_user_cleanup(&fs->vhost_user);
    virtio_delete_queue(fs->hiprio_vq);
    for (i = 0; i < fs->conf.num_request_queues; i++) {
        virtio_delete_queue(fs->req_vqs[i]);
    }
    g_free(fs->req_vqs);
    virtio_cleanup(vdev);
    g_free(fs->vhost_dev.vqs);
    return;
}

static void vuf_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserFS *fs = VHOST_USER_FS(dev);
    int i;

    /* This will stop vhost backend if appropriate. */
    vuf_set_status(vdev, 0);

    vhost_dev_cleanup(&fs->vhost_dev);

    vhost_user_cleanup(&fs->vhost_user);

    virtio_delete_queue(fs->hiprio_vq);
    for (i = 0; i < fs->conf.num_request_queues; i++) {
        virtio_delete_queue(fs->req_vqs[i]);
    }
    g_free(fs->req_vqs);
    virtio_cleanup(vdev);
    g_free(fs->vhost_dev.vqs);
    fs->vhost_dev.vqs = NULL;
}

static struct vhost_dev *vuf_get_vhost(VirtIODevice *vdev)
{
    VHostUserFS *fs = VHOST_USER_FS(vdev);
    return &fs->vhost_dev;
}

static const VMStateDescription vuf_vmstate = {
    .name = "vhost-user-fs",
    .unmigratable = 1,
};

static Property vuf_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserFS, conf.chardev),
    DEFINE_PROP_STRING("tag", VHostUserFS, conf.tag),
    DEFINE_PROP_UINT16("num-request-queues", VHostUserFS,
                       conf.num_request_queues, 1),
    DEFINE_PROP_UINT16("queue-size", VHostUserFS, conf.queue_size, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static void vuf_instance_init(Object *obj)
{
    VHostUserFS *fs = VHOST_USER_FS(obj);

    device_add_bootindex_property(obj, &fs->bootindex, "bootindex",
                                  "/filesystem@0", DEVICE(obj));
}

static void vuf_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vuf_properties);
    dc->vmsd = &vuf_vmstate;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vuf_device_realize;
    vdc->unrealize = vuf_device_unrealize;
    vdc->get_features = vuf_get_features;
    vdc->get_config = vuf_get_config;
    vdc->set_status = vuf_set_status;
    vdc->guest_notifier_mask = vuf_guest_notifier_mask;
    vdc->guest_notifier_pending = vuf_guest_notifier_pending;
    vdc->get_vhost = vuf_get_vhost;
}

static const TypeInfo vuf_info = {
    .name = TYPE_VHOST_USER_FS,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserFS),
    .instance_init = vuf_instance_init,
    .class_init = vuf_class_init,
};

static void vuf_register_types(void)
{
    type_register_static(&vuf_info);
}

type_init(vuf_register_types)
