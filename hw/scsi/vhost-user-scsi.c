/*
 * vhost-user-scsi host device
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 *
 * Author:
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This work is largely based on the "vhost-scsi" implementation by:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/fw-path-provider.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/vhost-user-scsi.h"
#include "hw/virtio/virtio.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"

/* Features supported by the host application */
static const int user_feature_bits[] = {
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_SCSI_F_HOTPLUG,
    VIRTIO_F_RING_RESET,
    VHOST_INVALID_FEATURE_BIT
};

static int vhost_user_scsi_start(VHostUserSCSI *s, Error **errp)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    int ret;

    ret = vhost_scsi_common_start(vsc, errp);
    s->started_vu = !(ret < 0);

    return ret;
}

static void vhost_user_scsi_stop(VHostUserSCSI *s)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);

    if (!s->started_vu) {
        return;
    }
    s->started_vu = false;

    vhost_scsi_common_stop(vsc);
}

static void vhost_user_scsi_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserSCSI *s = (VHostUserSCSI *)vdev;
    DeviceState *dev = DEVICE(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    bool should_start = virtio_device_should_start(vdev, status);
    Error *local_err = NULL;
    int ret;

    if (!s->connected) {
        return;
    }

    if (vhost_dev_is_started(&vsc->dev) == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_user_scsi_start(s, &local_err);
        if (ret < 0) {
            error_reportf_err(local_err,
                              "unable to start vhost-user-scsi: %s: ",
                              strerror(-ret));
            qemu_chr_fe_disconnect(&vs->conf.chardev);
        }
    } else {
        vhost_user_scsi_stop(s);
    }
}

static void vhost_user_scsi_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VHostUserSCSI *s = (VHostUserSCSI *)vdev;
    DeviceState *dev = DEVICE(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);

    Error *local_err = NULL;
    int i, ret;

    if (!vdev->start_on_kick) {
        return;
    }

    if (!s->connected) {
        return;
    }

    if (vhost_dev_is_started(&vsc->dev)) {
        return;
    }

    /*
     * Some guests kick before setting VIRTIO_CONFIG_S_DRIVER_OK so start
     * vhost here instead of waiting for .set_status().
     */
    ret = vhost_user_scsi_start(s, &local_err);
    if (ret < 0) {
        error_reportf_err(local_err, "vhost-user-scsi: vhost start failed: ");
        qemu_chr_fe_disconnect(&vs->conf.chardev);
        return;
    }

    /* Kick right away to begin processing requests already in vring */
    for (i = 0; i < vsc->dev.nvqs; i++) {
        VirtQueue *kick_vq = virtio_get_queue(vdev, i);

        if (!virtio_queue_get_desc_addr(vdev, i)) {
            continue;
        }
        event_notifier_set(virtio_queue_get_host_notifier(kick_vq));
    }
}

static int vhost_user_scsi_connect(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    int ret = 0;

    if (s->connected) {
        return 0;
    }

    vsc->dev.num_queues = vs->conf.num_queues;
    vsc->dev.nvqs = VIRTIO_SCSI_VQ_NUM_FIXED + vs->conf.num_queues;
    vsc->dev.vqs = s->vhost_vqs;
    vsc->dev.vq_index = 0;
    vsc->dev.backend_features = 0;

    ret = vhost_dev_init(&vsc->dev, &s->vhost_user, VHOST_BACKEND_TYPE_USER, 0,
                         errp);
    if (ret < 0) {
        return ret;
    }

    s->connected = true;

    /* restore vhost state */
    if (virtio_device_started(vdev, vdev->status)) {
        ret = vhost_user_scsi_start(s, errp);
    }

    return ret;
}

static void vhost_user_scsi_event(void *opaque, QEMUChrEvent event);

static void vhost_user_scsi_disconnect(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);

    if (!s->connected) {
        return;
    }
    s->connected = false;

    vhost_user_scsi_stop(s);

    vhost_dev_cleanup(&vsc->dev);

    /* Re-instate the event handler for new connections */
    qemu_chr_fe_set_handlers(&vs->conf.chardev, NULL, NULL,
                             vhost_user_scsi_event, NULL, dev, NULL, true);
}

static void vhost_user_scsi_event(void *opaque, QEMUChrEvent event)
{
    DeviceState *dev = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    Error *local_err = NULL;

    switch (event) {
    case CHR_EVENT_OPENED:
        if (vhost_user_scsi_connect(dev, &local_err) < 0) {
            error_report_err(local_err);
            qemu_chr_fe_disconnect(&vs->conf.chardev);
            return;
        }
        break;
    case CHR_EVENT_CLOSED:
        /* defer close until later to avoid circular close */
        vhost_user_async_close(dev, &vs->conf.chardev, &vsc->dev,
                               vhost_user_scsi_disconnect,
                               vhost_user_scsi_event);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static int vhost_user_scsi_realize_connect(VHostUserSCSI *s, Error **errp)
{
    DeviceState *dev = DEVICE(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    int ret;

    s->connected = false;

    ret = qemu_chr_fe_wait_connected(&vs->conf.chardev, errp);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_user_scsi_connect(dev, errp);
    if (ret < 0) {
        qemu_chr_fe_disconnect(&vs->conf.chardev);
        return ret;
    }
    assert(s->connected);

    return 0;
}

static void vhost_user_scsi_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(dev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    Error *err = NULL;
    int ret;
    int retries = VU_REALIZE_CONN_RETRIES;

    if (!vs->conf.chardev.chr) {
        error_setg(errp, "vhost-user-scsi: missing chardev");
        return;
    }

    virtio_scsi_common_realize(dev, vhost_user_scsi_handle_output,
                               vhost_user_scsi_handle_output,
                               vhost_user_scsi_handle_output, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    if (!vhost_user_init(&s->vhost_user, &vs->conf.chardev, errp)) {
        goto free_virtio;
    }

    vsc->inflight = g_new0(struct vhost_inflight, 1);
    s->vhost_vqs = g_new0(struct vhost_virtqueue,
                          VIRTIO_SCSI_VQ_NUM_FIXED + vs->conf.num_queues);

    assert(!*errp);
    do {
        if (*errp) {
            error_prepend(errp, "Reconnecting after error: ");
            error_report_err(*errp);
            *errp = NULL;
        }
        ret = vhost_user_scsi_realize_connect(s, errp);
    } while (ret < 0 && retries--);

    if (ret < 0) {
        goto free_vhost;
    }

    /* we're fully initialized, now we can operate, so add the handler */
    qemu_chr_fe_set_handlers(&vs->conf.chardev,  NULL, NULL,
                             vhost_user_scsi_event, NULL, (void *)dev,
                             NULL, true);
    /* Channel and lun both are 0 for bootable vhost-user-scsi disk */
    vsc->channel = 0;
    vsc->lun = 0;
    vsc->target = vs->conf.boot_tpgt;

    return;

free_vhost:
    g_free(s->vhost_vqs);
    s->vhost_vqs = NULL;
    g_free(vsc->inflight);
    vsc->inflight = NULL;
    vhost_user_cleanup(&s->vhost_user);

free_virtio:
    virtio_scsi_common_unrealize(dev);
}

static void vhost_user_scsi_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(dev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);

    /* This will stop the vhost backend. */
    vhost_user_scsi_set_status(vdev, 0);
    qemu_chr_fe_set_handlers(&vs->conf.chardev, NULL, NULL, NULL, NULL, NULL,
                             NULL, false);

    vhost_dev_cleanup(&vsc->dev);
    g_free(s->vhost_vqs);
    s->vhost_vqs = NULL;

    vhost_dev_free_inflight(vsc->inflight);
    g_free(vsc->inflight);
    vsc->inflight = NULL;

    vhost_user_cleanup(&s->vhost_user);
    virtio_scsi_common_unrealize(dev);
}

static Property vhost_user_scsi_properties[] = {
    DEFINE_PROP_CHR("chardev", VirtIOSCSICommon, conf.chardev),
    DEFINE_PROP_UINT32("boot_tpgt", VirtIOSCSICommon, conf.boot_tpgt, 0),
    DEFINE_PROP_UINT32("num_queues", VirtIOSCSICommon, conf.num_queues,
                       VIRTIO_SCSI_AUTO_NUM_QUEUES),
    DEFINE_PROP_UINT32("virtqueue_size", VirtIOSCSICommon, conf.virtqueue_size,
                       128),
    DEFINE_PROP_UINT32("max_sectors", VirtIOSCSICommon, conf.max_sectors,
                       0xFFFF),
    DEFINE_PROP_UINT32("cmd_per_lun", VirtIOSCSICommon, conf.cmd_per_lun, 128),
    DEFINE_PROP_BIT64("hotplug", VHostSCSICommon, host_features,
                                                  VIRTIO_SCSI_F_HOTPLUG,
                                                  true),
    DEFINE_PROP_BIT64("param_change", VHostSCSICommon, host_features,
                                                       VIRTIO_SCSI_F_CHANGE,
                                                       true),
    DEFINE_PROP_BIT64("t10_pi", VHostSCSICommon, host_features,
                                                 VIRTIO_SCSI_F_T10_PI,
                                                 false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_scsi_reset(VirtIODevice *vdev)
{
    VHostUserSCSI *s = VHOST_USER_SCSI(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);

    vhost_dev_free_inflight(vsc->inflight);
}

static struct vhost_dev *vhost_user_scsi_get_vhost(VirtIODevice *vdev)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(vdev);
    return &vsc->dev;
}

static const VMStateDescription vmstate_vhost_scsi = {
    .name = "virtio-scsi",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void vhost_user_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(klass);

    device_class_set_props(dc, vhost_user_scsi_properties);
    dc->vmsd = &vmstate_vhost_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_scsi_realize;
    vdc->unrealize = vhost_user_scsi_unrealize;
    vdc->get_features = vhost_scsi_common_get_features;
    vdc->set_config = vhost_scsi_common_set_config;
    vdc->set_status = vhost_user_scsi_set_status;
    fwc->get_dev_path = vhost_scsi_common_get_fw_dev_path;
    vdc->reset = vhost_user_scsi_reset;
    vdc->get_vhost = vhost_user_scsi_get_vhost;
}

static void vhost_user_scsi_instance_init(Object *obj)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(obj);

    vsc->feature_bits = user_feature_bits;

    /* Add the bootindex property for this object */
    device_add_bootindex_property(obj, &vsc->bootindex, "bootindex", NULL,
                                  DEVICE(vsc));
}

static const TypeInfo vhost_user_scsi_info = {
    .name = TYPE_VHOST_USER_SCSI,
    .parent = TYPE_VHOST_SCSI_COMMON,
    .instance_size = sizeof(VHostUserSCSI),
    .class_init = vhost_user_scsi_class_init,
    .instance_init = vhost_user_scsi_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { }
    },
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_user_scsi_info);
}

type_init(virtio_register_types)
