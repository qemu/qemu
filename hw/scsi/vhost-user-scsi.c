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
#include "qom/object.h"
#include "hw/fw-path-provider.h"
#include "hw/qdev-core.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/vhost-user-scsi.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-access.h"
#include "chardev/char-fe.h"

/* Features supported by the host application */
static const int user_feature_bits[] = {
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_SCSI_F_HOTPLUG,
    VHOST_INVALID_FEATURE_BIT
};

static void vhost_user_scsi_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostUserSCSI *s = (VHostUserSCSI *)vdev;
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    bool start = (status & VIRTIO_CONFIG_S_DRIVER_OK) && vdev->vm_running;

    if (vsc->dev.started == start) {
        return;
    }

    if (start) {
        int ret;

        ret = vhost_scsi_common_start(vsc);
        if (ret < 0) {
            error_report("unable to start vhost-user-scsi: %s", strerror(-ret));
            exit(1);
        }
    } else {
        vhost_scsi_common_stop(vsc);
    }
}

static void vhost_dummy_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
}

static void vhost_user_scsi_realize(DeviceState *dev, Error **errp)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(dev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    VhostUserState *user;
    Error *err = NULL;
    int ret;

    if (!vs->conf.chardev.chr) {
        error_setg(errp, "vhost-user-scsi: missing chardev");
        return;
    }

    virtio_scsi_common_realize(dev, vhost_dummy_handle_output,
                               vhost_dummy_handle_output,
                               vhost_dummy_handle_output, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    user = vhost_user_init();
    if (!user) {
        error_setg(errp, "vhost-user-scsi: failed to init vhost_user");
        return;
    }
    user->chr = &vs->conf.chardev;

    vsc->dev.nvqs = 2 + vs->conf.num_queues;
    vsc->dev.vqs = g_new(struct vhost_virtqueue, vsc->dev.nvqs);
    vsc->dev.vq_index = 0;
    vsc->dev.backend_features = 0;

    ret = vhost_dev_init(&vsc->dev, user,
                         VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_setg(errp, "vhost-user-scsi: vhost initialization failed: %s",
                   strerror(-ret));
        vhost_user_cleanup(user);
        g_free(user);
        return;
    }

    s->vhost_user = user;

    /* Channel and lun both are 0 for bootable vhost-user-scsi disk */
    vsc->channel = 0;
    vsc->lun = 0;
    vsc->target = vs->conf.boot_tpgt;
}

static void vhost_user_scsi_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserSCSI *s = VHOST_USER_SCSI(dev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);

    /* This will stop the vhost backend. */
    vhost_user_scsi_set_status(vdev, 0);

    vhost_dev_cleanup(&vsc->dev);
    g_free(vsc->dev.vqs);

    virtio_scsi_common_unrealize(dev, errp);

    if (s->vhost_user) {
        vhost_user_cleanup(s->vhost_user);
        g_free(s->vhost_user);
        s->vhost_user = NULL;
    }
}

static Property vhost_user_scsi_properties[] = {
    DEFINE_PROP_CHR("chardev", VirtIOSCSICommon, conf.chardev),
    DEFINE_PROP_UINT32("boot_tpgt", VirtIOSCSICommon, conf.boot_tpgt, 0),
    DEFINE_PROP_UINT32("num_queues", VirtIOSCSICommon, conf.num_queues, 1),
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

static const VMStateDescription vmstate_vhost_scsi = {
    .name = "virtio-scsi",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void vhost_user_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(klass);

    dc->props = vhost_user_scsi_properties;
    dc->vmsd = &vmstate_vhost_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_user_scsi_realize;
    vdc->unrealize = vhost_user_scsi_unrealize;
    vdc->get_features = vhost_scsi_common_get_features;
    vdc->set_config = vhost_scsi_common_set_config;
    vdc->set_status = vhost_user_scsi_set_status;
    fwc->get_dev_path = vhost_scsi_common_get_fw_dev_path;
}

static void vhost_user_scsi_instance_init(Object *obj)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(obj);

    vsc->feature_bits = user_feature_bits;

    /* Add the bootindex property for this object */
    device_add_bootindex_property(obj, &vsc->bootindex, "bootindex", NULL,
                                  DEVICE(vsc), NULL);
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
