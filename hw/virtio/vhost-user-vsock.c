/*
 * Vhost-user vsock virtio device
 *
 * Copyright 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-vsock.h"

static const int user_feature_bits[] = {
    VIRTIO_F_VERSION_1,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VHOST_INVALID_FEATURE_BIT
};

static void vuv_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostUserVSock *vsock = VHOST_USER_VSOCK(vdev);

    memcpy(config, &vsock->vsockcfg, sizeof(struct virtio_vsock_config));
}

static int vuv_handle_config_change(struct vhost_dev *dev)
{
    VHostUserVSock *vsock = VHOST_USER_VSOCK(dev->vdev);
    int ret = vhost_dev_get_config(dev, (uint8_t *)&vsock->vsockcfg,
                                   sizeof(struct virtio_vsock_config));
    if (ret < 0) {
        error_report("get config space failed");
        return -1;
    }

    virtio_notify_config(dev->vdev);

    return 0;
}

const VhostDevConfigOps vsock_ops = {
    .vhost_dev_config_notifier = vuv_handle_config_change,
};

static void vuv_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (vvc->vhost_dev.started == should_start) {
        return;
    }

    if (should_start) {
        int ret = vhost_vsock_common_start(vdev);
        if (ret < 0) {
            return;
        }
    } else {
        vhost_vsock_common_stop(vdev);
    }
}

static uint64_t vuv_get_features(VirtIODevice *vdev,
                                 uint64_t features,
                                 Error **errp)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);

    return vhost_get_features(&vvc->vhost_dev, user_feature_bits, features);
}

static const VMStateDescription vuv_vmstate = {
    .name = "vhost-user-vsock",
    .unmigratable = 1,
};

static void vuv_device_realize(DeviceState *dev, Error **errp)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserVSock *vsock = VHOST_USER_VSOCK(dev);
    int ret;

    if (!vsock->conf.chardev.chr) {
        error_setg(errp, "missing chardev");
        return;
    }

    if (!vhost_user_init(&vsock->vhost_user, &vsock->conf.chardev, errp)) {
        return;
    }

    vhost_vsock_common_realize(vdev, "vhost-user-vsock");

    vhost_dev_set_config_notifier(&vvc->vhost_dev, &vsock_ops);

    ret = vhost_dev_init(&vvc->vhost_dev, &vsock->vhost_user,
                         VHOST_BACKEND_TYPE_USER, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost_dev_init failed");
        goto err_virtio;
    }

    ret = vhost_dev_get_config(&vvc->vhost_dev, (uint8_t *)&vsock->vsockcfg,
                               sizeof(struct virtio_vsock_config));
    if (ret < 0) {
        error_setg_errno(errp, -ret, "get config space failed");
        goto err_vhost_dev;
    }

    return;

err_vhost_dev:
    vhost_dev_cleanup(&vvc->vhost_dev);
err_virtio:
    vhost_vsock_common_unrealize(vdev);
    vhost_user_cleanup(&vsock->vhost_user);
    return;
}

static void vuv_device_unrealize(DeviceState *dev)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostUserVSock *vsock = VHOST_USER_VSOCK(dev);

    /* This will stop vhost backend if appropriate. */
    vuv_set_status(vdev, 0);

    vhost_dev_cleanup(&vvc->vhost_dev);

    vhost_vsock_common_unrealize(vdev);

    vhost_user_cleanup(&vsock->vhost_user);

}

static Property vuv_properties[] = {
    DEFINE_PROP_CHR("chardev", VHostUserVSock, conf.chardev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vuv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vuv_properties);
    dc->vmsd = &vuv_vmstate;
    vdc->realize = vuv_device_realize;
    vdc->unrealize = vuv_device_unrealize;
    vdc->get_features = vuv_get_features;
    vdc->get_config = vuv_get_config;
    vdc->set_status = vuv_set_status;
}

static const TypeInfo vuv_info = {
    .name = TYPE_VHOST_USER_VSOCK,
    .parent = TYPE_VHOST_VSOCK_COMMON,
    .instance_size = sizeof(VHostUserVSock),
    .class_init = vuv_class_init,
};

static void vuv_register_types(void)
{
    type_register_static(&vuv_info);
}

type_init(vuv_register_types)
