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
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/virtio-blk-common.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user-blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

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
    VIRTIO_BLK_F_DISCARD,
    VIRTIO_BLK_F_WRITE_ZEROES,
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_F_RING_PACKED,
    VIRTIO_F_IOMMU_PLATFORM,
    VIRTIO_F_RING_RESET,
    VHOST_INVALID_FEATURE_BIT
};

static void vhost_user_blk_event(void *opaque, QEMUChrEvent event);

static void vhost_user_blk_update_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);

    /* Our num_queues overrides the device backend */
    virtio_stw_p(vdev, &s->blkcfg.num_queues, s->num_queues);

    memcpy(config, &s->blkcfg, vdev->config_len);
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
                               VHOST_SET_CONFIG_TYPE_FRONTEND);
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
    VirtIODevice *vdev = dev->vdev;
    VHostUserBlk *s = VHOST_USER_BLK(dev->vdev);
    Error *local_err = NULL;

    if (!dev->started) {
        return 0;
    }

    ret = vhost_dev_get_config(dev, (uint8_t *)&blkcfg,
                               vdev->config_len, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return ret;
    }

    /* valid for resize only */
    if (blkcfg.capacity != s->blkcfg.capacity) {
        s->blkcfg.capacity = blkcfg.capacity;
        memcpy(dev->vdev->config, &s->blkcfg, vdev->config_len);
        virtio_notify_config(dev->vdev);
    }

    return 0;
}

const VhostDevConfigOps blk_ops = {
    .vhost_dev_config_notifier = vhost_user_blk_handle_config_change,
};

static int vhost_user_blk_start(VirtIODevice *vdev, Error **errp)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int i, ret;

    if (!k->set_guest_notifiers) {
        error_setg(errp, "binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&s->dev, vdev);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error enabling host notifiers");
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, true);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error binding guest notifier");
        goto err_host_notifiers;
    }

    s->dev.acked_features = vdev->guest_features;

    ret = vhost_dev_prepare_inflight(&s->dev, vdev);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error setting inflight format");
        goto err_guest_notifiers;
    }

    if (!s->inflight->addr) {
        ret = vhost_dev_get_inflight(&s->dev, s->queue_size, s->inflight);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Error getting inflight");
            goto err_guest_notifiers;
        }
    }

    ret = vhost_dev_set_inflight(&s->dev, s->inflight);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error setting inflight");
        goto err_guest_notifiers;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    s->dev.vq_index_end = s->dev.nvqs;
    ret = vhost_dev_start(&s->dev, vdev, true);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error starting vhost");
        goto err_guest_notifiers;
    }
    s->started_vu = true;

    return ret;

err_guest_notifiers:
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, true);
    }
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);
    return ret;
}

static void vhost_user_blk_stop(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!s->started_vu) {
        return;
    }
    s->started_vu = false;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&s->dev, vdev, true);

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
    bool should_start = virtio_device_should_start(vdev, status);
    Error *local_err = NULL;
    int ret;

    if (!s->connected) {
        return;
    }

    if (vhost_dev_is_started(&s->dev) == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_user_blk_start(vdev, &local_err);
        if (ret < 0) {
            error_reportf_err(local_err, "vhost-user-blk: vhost start failed: ");
            qemu_chr_fe_disconnect(&s->chardev);
        }
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
    virtio_add_feature(&features, VIRTIO_BLK_F_SIZE_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_SEG_MAX);
    virtio_add_feature(&features, VIRTIO_BLK_F_GEOMETRY);
    virtio_add_feature(&features, VIRTIO_BLK_F_TOPOLOGY);
    virtio_add_feature(&features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_add_feature(&features, VIRTIO_BLK_F_FLUSH);
    virtio_add_feature(&features, VIRTIO_BLK_F_RO);

    if (s->num_queues > 1) {
        virtio_add_feature(&features, VIRTIO_BLK_F_MQ);
    }

    return vhost_get_features(&s->dev, user_feature_bits, features);
}

static void vhost_user_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    Error *local_err = NULL;
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (!s->connected) {
        return;
    }

    if (vhost_dev_is_started(&s->dev)) {
        return;
    }

    /* Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
     * vhost here instead of waiting for .set_status().
     */
    ret = vhost_user_blk_start(vdev, &local_err);
    if (ret < 0) {
        error_reportf_err(local_err, "vhost-user-blk: vhost start failed: ");
        qemu_chr_fe_disconnect(&s->chardev);
        return;
    }

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < s->dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static void vhost_user_blk_reset(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);

    vhost_dev_free_inflight(s->inflight);
}

static int vhost_user_blk_connect(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    int ret = 0;

    if (s->connected) {
        return 0;
    }

    s->dev.num_queues = s->num_queues;
    s->dev.nvqs = s->num_queues;
    s->dev.vqs = s->vhost_vqs;
    s->dev.vq_index = 0;
    s->dev.backend_features = 0;

    vhost_dev_set_config_notifier(&s->dev, &blk_ops);

    s->vhost_user.supports_config = true;
    ret = vhost_dev_init(&s->dev, &s->vhost_user, VHOST_BACKEND_TYPE_USER, 0,
                         errp);
    if (ret < 0) {
        return ret;
    }

    s->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        ret = vhost_user_blk_start(vdev, errp);
    }

    return ret;
}

static void vhost_user_blk_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);

    if (!s->connected) {
        return;
    }
    s->connected = false;

    vhost_user_blk_stop(vdev);

    vhost_dev_cleanup(&s->dev);

    /* Re-instate the event handler for new connections */
    qemu_chr_fe_set_handlers(&s->chardev, NULL, NULL, vhost_user_blk_event,
                             NULL, dev, NULL, true);
}

static void vhost_user_blk_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    Error *local_err = NULL;

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vhost_user_blk_connect(dev, &local_err) < 0) {
            error_report_err(local_err);
            qemu_chr_fe_disconnect(&s->chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        /* defer close until later to avoid circular close */
        vhost_user_async_close(dev, &s->chardev, &s->dev,
                               vhost_user_blk_disconnect, vhost_user_blk_event);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static int vhost_user_blk_realize_connect(VHostUserBlk *s, Error **errp)
{
    DeviceState *dev = DEVICE(s);
    int ret;

    s->connected = false;

    ret = qemu_chr_fe_wait_connected(&s->chardev, errp);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_user_blk_connect(dev, errp);
    if (ret < 0) {
        qemu_chr_fe_disconnect(&s->chardev);
        return ret;
    }
    assert(s->connected);

    ret = vhost_dev_get_config(&s->dev, (uint8_t *)&s->blkcfg,
                               VIRTIO_DEVICE(s)->config_len, errp);
    if (ret < 0) {
        qemu_chr_fe_disconnect(&s->chardev);
        vhost_dev_cleanup(&s->dev);
        return ret;
    }

    return 0;
}

static void vhost_user_blk_device_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    size_t config_size;
    int retries;
    int i, ret;

    if (!s->chardev.chr) {
        error_setg(errp, "chardev is mandatory");
        return;
    }

    if (s->num_queues == VHOST_USER_BLK_AUTO_NUM_QUEUES) {
        s->num_queues = 1;
    }
    if (!s->num_queues || s->num_queues > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "invalid number of IO queues");
        return;
    }

    if (!s->queue_size) {
        error_setg(errp, "queue size must be non-zero");
        return;
    }
    if (s->queue_size > VIRTQUEUE_MAX_SIZE) {
        error_setg(errp, "queue size must not exceed %d",
                   VIRTQUEUE_MAX_SIZE);
        return;
    }

    if (!vhost_user_init(&s->vhost_user, &s->chardev, errp)) {
        return;
    }

    config_size = virtio_get_config_size(&virtio_blk_cfg_size_params,
                                         vdev->host_features);
    virtio_init(vdev, VIRTIO_ID_BLOCK, config_size);

    s->virtqs = g_new(VirtQueue *, s->num_queues);
    for (i = 0; i < s->num_queues; i++) {
        s->virtqs[i] = virtio_add_queue(vdev, s->queue_size,
                                        vhost_user_blk_handle_output);
    }

    s->inflight = g_new0(struct vhost_inflight, 1);
    s->vhost_vqs = g_new0(struct vhost_virtqueue, s->num_queues);

    retries = VU_REALIZE_CONN_RETRIES;
    assert(!*errp);
    do {
        if (*errp) {
            error_prepend(errp, "Reconnecting after error: ");
            error_report_err(*errp);
            *errp = NULL;
        }
        ret = vhost_user_blk_realize_connect(s, errp);
    } while (ret < 0 && retries--);

    if (ret < 0) {
        goto virtio_err;
    }

    /* we're fully initialized, now we can operate, so add the handler */
    qemu_chr_fe_set_handlers(&s->chardev,  NULL, NULL,
                             vhost_user_blk_event, NULL, (void *)dev,
                             NULL, true);
    return;

virtio_err:
    g_free(s->vhost_vqs);
    s->vhost_vqs = NULL;
    g_free(s->inflight);
    s->inflight = NULL;
    for (i = 0; i < s->num_queues; i++) {
        virtio_delete_queue(s->virtqs[i]);
    }
    g_free(s->virtqs);
    virtio_cleanup(vdev);
    vhost_user_cleanup(&s->vhost_user);
}

static void vhost_user_blk_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserBlk *s = VHOST_USER_BLK(dev);
    int i;

    virtio_set_status(vdev, 0);
    qemu_chr_fe_set_handlers(&s->chardev,  NULL, NULL, NULL,
                             NULL, NULL, NULL, false);
    vhost_dev_cleanup(&s->dev);
    vhost_dev_free_inflight(s->inflight);
    g_free(s->vhost_vqs);
    s->vhost_vqs = NULL;
    g_free(s->inflight);
    s->inflight = NULL;

    for (i = 0; i < s->num_queues; i++) {
        virtio_delete_queue(s->virtqs[i]);
    }
    g_free(s->virtqs);
    virtio_cleanup(vdev);
    vhost_user_cleanup(&s->vhost_user);
}

static void vhost_user_blk_instance_init(Object *obj)
{
    VHostUserBlk *s = VHOST_USER_BLK(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj));
}

static struct vhost_dev *vhost_user_blk_get_vhost(VirtIODevice *vdev)
{
    VHostUserBlk *s = VHOST_USER_BLK(vdev);
    return &s->dev;
}

static const VMStateDescription vmstate_vhost_user_blk = {
    .name = "vhost-user-blk",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property vhost_user_blk_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserBlk, chardev),
    DEFINE_PROP_UINT16("num-queues", VHostUserBlk, num_queues,
                       VHOST_USER_BLK_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT32("queue-size", VHostUserBlk, queue_size, 128),
    DEFINE_PROP_BIT64("config-wce", VHostUserBlk, parent_obj.host_features,
                      VIRTIO_BLK_F_CONFIG_WCE, true),
    DEFINE_PROP_BIT64("discard", VHostUserBlk, parent_obj.host_features,
                      VIRTIO_BLK_F_DISCARD, true),
    DEFINE_PROP_BIT64("write-zeroes", VHostUserBlk, parent_obj.host_features,
                      VIRTIO_BLK_F_WRITE_ZEROES, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_user_blk_properties);
    dc->vmsd = &vmstate_vhost_user_blk;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_blk_device_realize;
    vdc->unrealize = vhost_user_blk_device_unrealize;
    vdc->get_config = vhost_user_blk_update_config;
    vdc->set_config = vhost_user_blk_set_config;
    vdc->get_features = vhost_user_blk_get_features;
    vdc->set_status = vhost_user_blk_set_status;
    vdc->reset = vhost_user_blk_reset;
    vdc->get_vhost = vhost_user_blk_get_vhost;
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
