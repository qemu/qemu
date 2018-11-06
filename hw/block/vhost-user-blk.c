/*
 * vhost-user-blk host device
 *
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * Largely based on the "vhost-user-scsi.c" and "vhost-scsi.c" implemented by:
 * Felipe Franciosi <felipe@nutanix.com>
 * Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
 * Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

static const int user_feature_bits[] = {
    VIRTIO_BLK_F_SIZE_MAX,
    VIRTIO_BLK_F_SEG_MAX,
    VIRTIO_BLK_F_GEOMETRY,
    VIRTIO_BLK_F_BLK_SIZE,
    VIRTIO_BLK_F_TOPOLOGY,
    VIRTIO_BLK_F_MQ,
    VIRTIO_BLK_F_RO,
    VIRTIO_BLK_F_FLUSH,
    VIRTIO_BLK_F_CONFIG_WCE,
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VHOST_INVALID_FEATURE_BIT
};

static void vhost_user_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);

    memcpy(config, &s->blkcfg, sizeof(struct virtio_blk_config));
}

static void vhost_user_blk_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    struct virtio_blk_config *blkcfg = (struct virtio_blk_config *)config;
    int ret;

    if (blkcfg->wce == s->blkcfg.wce) {
        return;
    }

    ret = vhost_dev_set_config(&s->dev, &blkcfg->wce,
                               offsetof(struct virtio_blk_config, wce),
                               sizeof(blkcfg->wce),
                               VHOST_SET_CONFIG_TYPE_MASTER);
    if (ret) {
        error_report("set device config space failed");
        return;
    }

    s->blkcfg.wce = blkcfg->wce;
}

static int vhost_user_blk_handle_config_change(struct vhost_dev *dev)
{
    int ret;
    struct virtio_blk_config blkcfg;
    VHostUserBlk *s = VHOST_USER_BLK(dev->vdev);

    ret = vhost_dev_get_config(dev, (uint8_t *)&blkcfg,
                               sizeof(struct virtio_blk_config));
    if (ret < 0) {
        error_report("get config space failed");
        return -1;
    }

    /* valid for resize only */
    if (blkcfg.capacity != s->blkcfg.capacity) {
        s->blkcfg.capacity = blkcfg.capacity;
        memcpy(dev->vdev->config, &s->blkcfg, sizeof(struct virtio_blk_config));
        virtio_notify_config(dev->vdev);
    }

    return 0;
}

const VhostDevConfigOps blk_ops = {
    .vhost_dev_config_notifier = vhost_user_blk_handle_config_change,
};

static void vhost_user_blk_start(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&s->dev, vdev);
    if (ret < 0) {
        error_report("Error enabling host notifiers: %d", -ret);
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier: %d", -ret);
        goto err_host_notifiers;
    }

    s->dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&s->dev, vdev);
    if (ret < 0) {
        error_report("Error starting vhost: %d", -ret);
        goto err_guest_notifiers;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static void vhost_user_blk_stop(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&s->dev, vdev);

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static void vhost_user_blk_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (s->dev.started == should_start) {
        return;
    }

    if (should_start) {
        vhost_user_blk_start(vdev);
    } else {
        vhost_user_blk_stop(vdev);
    }

}

static uint64_t vhost_user_blk_get_features(VirtIODevice *vdev,
                                            uint64_t features,
                                            Error **errp)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);

    /* Turn on pre-defined features */
    virtio_add_feature(&features, VIRTIO_BLK_F_SEG_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_GEOMETRY);
    virtio_add_feature(&features, VIRTIO_BLK_F_TOPOLOGY);
    virtio_add_feature(&features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_add_feature(&features, VIRTIO_BLK_F_FLUSH);
    virtio_add_feature(&features, VIRTIO_BLK_F_RO);

    if (s->config_wce) {
        virtio_add_feature(&features, VIRTIO_BLK_F_CONFIG_WCE);
    }
    if (s->num_queues > 1) {
        virtio_add_feature(&features, VIRTIO_BLK_F_MQ);
    }

    return vhost_get_features(&s->dev, user_feature_bits, features);
}

static void vhost_user_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    int i;

    if (!(virtio_host_has_feature(vdev, VIRTIO_F_VERSION_1) &&
        !virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1))) {
        return;
    }

    if (s->dev.started) {
        return;
    }

    /* Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
     * vhost here instead of waiting for .set_status().
     */
    vhost_user_blk_start(vdev);

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < s->dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static void vhost_user_blk_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    VhostUserState *user;
    int i, ret;

    if (!s->chardev.chr) {
        error_setg(errp, "vhost-user-blk: chardev is mandatory");
        return;
    }

    if (!s->num_queues || s->num_queues > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "vhost-user-blk: invalid number of IO queues");
        return;
    }

    if (!s->queue_size) {
        error_setg(errp, "vhost-user-blk: queue size must be non-zero");
        return;
    }

    user = vhost_user_init();
    if (!user) {
        error_setg(errp, "vhost-user-blk: failed to init vhost_user");
        return;
    }

    user->chr = &s->chardev;
    s->vhost_user = user;

    virtio_init(vdev, "virtio-blk", VIRTIO_ID_BLOCK,
                sizeof(struct virtio_blk_config));

    for (i = 0; i < s->num_queues; i++) {
        virtio_add_queue(vdev, s->queue_size,
                         vhost_user_blk_handle_output);
    }

    s->dev.nvqs = s->num_queues;
    s->dev.vqs = g_new(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;
    s->dev.backend_features = 0;

    vhost_dev_set_config_notifier(&s->dev, &blk_ops);

    ret = vhost_dev_init(&s->dev, s->vhost_user, VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_setg(errp, "vhost-user-blk: vhost initialization failed: %s",
                   strerror(-ret));
        goto virtio_err;
    }

    ret = vhost_dev_get_config(&s->dev, (uint8_t *)&s->blkcfg,
                              sizeof(struct virtio_blk_config));
    if (ret < 0) {
        error_setg(errp, "vhost-user-blk: get block config failed");
        goto vhost_err;
    }

    if (s->blkcfg.num_queues != s->num_queues) {
        s->blkcfg.num_queues = s->num_queues;
    }

    return;

vhost_err:
    vhost_dev_cleanup(&s->dev);
virtio_err:
    g_free(s->dev.vqs);
    virtio_cleanup(vdev);

    vhost_user_cleanup(user);
    g_free(user);
    s->vhost_user = NULL;
}

static void vhost_user_blk_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(dev);

    vhost_user_blk_set_status(vdev, 0);
    vhost_dev_cleanup(&s->dev);
    g_free(s->dev.vqs);
    virtio_cleanup(vdev);

    if (s->vhost_user) {
        vhost_user_cleanup(s->vhost_user);
        g_free(s->vhost_user);
        s->vhost_user = NULL;
    }
}

static void vhost_user_blk_instance_init(Object *obj)
{
    VHostUserBlk *s = VHOST_USER_BLK(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj), NULL);
}

static const VMStateDescription vmstate_vhost_user_blk = {
    .name = "vhost-user-blk",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_user_blk_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
    DEFINE_PROP_UINT16("num-queues", VHostUserBlk, num_queues, 1),
    DEFINE_PROP_UINT32("queue-size", VHostUserBlk, queue_size, 128),
    DEFINE_PROP_BIT("config-wce", VHostUserBlk, config_wce, 0, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = vhost_user_blk_properties;
    dc->vmsd = &vmstate_vhost_user_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_blk_device_realize;
    vdc->unrealize = vhost_user_blk_device_unrealize;
    vdc->get_config = vhost_user_blk_update_config;
    vdc->set_config = vhost_user_blk_set_config;
    vdc->get_features = vhost_user_blk_get_features;
    vdc->set_status = vhost_user_blk_set_status;
}

static const TypeInfo vhost_user_blk_info = {
    .name = TYPE_VHOST_USER_BLK,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostUserBlk),
    .instance_init = vhost_user_blk_instance_init,
    .class_init = vhost_user_blk_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_user_blk_info);
}

type_init(virtio_register_types)
