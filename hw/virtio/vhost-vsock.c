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

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_vsock.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-vsock.h"
#include "monitor/monitor.h"

static void vhost_vsock_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VHostVSock *vsock = VHOST_VSOCK(vdev);
    struct virtio_vsock_config vsockcfg = {};

    virtio_stq_p(vdev, &vsockcfg.guest_cid, vsock->conf.guest_cid);
    memcpy(config, &vsockcfg, sizeof(vsockcfg));
}

static int vhost_vsock_set_guest_cid(VirtIODevice *vdev)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);
    VHostVSock *vsock = VHOST_VSOCK(vdev);
    const VhostOps *vhost_ops = vvc->vhost_dev.vhost_ops;
    int ret;

    if (!vhost_ops->vhost_vsock_set_guest_cid) {
        return -ENOSYS;
    }

    ret = vhost_ops->vhost_vsock_set_guest_cid(&vvc->vhost_dev,
                                               vsock->conf.guest_cid);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

static int vhost_vsock_set_running(VirtIODevice *vdev, int start)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);
    const VhostOps *vhost_ops = vvc->vhost_dev.vhost_ops;
    int ret;

    if (!vhost_ops->vhost_vsock_set_running) {
        return -ENOSYS;
    }

    ret = vhost_ops->vhost_vsock_set_running(&vvc->vhost_dev, start);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}


static int vhost_vsock_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(vdev);
    bool should_start = virtio_device_should_start(vdev, status);
    int ret;

    if (vhost_dev_is_started(&vvc->vhost_dev) == should_start) {
        return 0;
    }

    if (should_start) {
        ret = vhost_vsock_common_start(vdev);
        if (ret < 0) {
            return 0;
        }

        ret = vhost_vsock_set_running(vdev, 1);
        if (ret < 0) {
            vhost_vsock_common_stop(vdev);
            error_report("Error starting vhost vsock: %d", -ret);
            return 0;
        }
    } else {
        ret = vhost_vsock_set_running(vdev, 0);
        if (ret < 0) {
            error_report("vhost vsock set running failed: %d", ret);
            return 0;
        }

        vhost_vsock_common_stop(vdev);
    }
    return 0;
}

static uint64_t vhost_vsock_get_features(VirtIODevice *vdev,
                                         uint64_t requested_features,
                                         Error **errp)
{
    return vhost_vsock_common_get_features(vdev, requested_features, errp);
}

static const VMStateDescription vmstate_virtio_vhost_vsock = {
    .name = "virtio-vhost_vsock",
    .minimum_version_id = VHOST_VSOCK_SAVEVM_VERSION,
    .version_id = VHOST_VSOCK_SAVEVM_VERSION,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
    .pre_save = vhost_vsock_common_pre_save,
    .post_load = vhost_vsock_common_post_load,
};

static void vhost_vsock_device_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(dev);
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
        vhostfd = monitor_fd_param(monitor_cur(), vsock->conf.vhostfd, errp);
        if (vhostfd == -1) {
            error_prepend(errp, "vhost-vsock: unable to parse vhostfd: ");
            return;
        }

        if (!qemu_set_blocking(vhostfd, false, errp)) {
            return;
        }
    } else {
        vhostfd = open("/dev/vhost-vsock", O_RDWR);
        if (vhostfd < 0) {
            error_setg_file_open(errp, errno, "/dev/vhost-vsock");
            return;
        }

        if (!qemu_set_blocking(vhostfd, false, errp)) {
            return;
        }
    }

    vhost_vsock_common_realize(vdev);

    ret = vhost_dev_init(&vvc->vhost_dev, (void *)(uintptr_t)vhostfd,
                         VHOST_BACKEND_TYPE_KERNEL, 0, errp);
    if (ret < 0) {
        /*
         * vhostfd is closed by vhost_dev_cleanup, which is called
         * by vhost_dev_init on initialization error.
         */
        goto err_virtio;
    }

    ret = vhost_vsock_set_guest_cid(vdev);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost-vsock: unable to set guest cid");
        goto err_vhost_dev;
    }

    return;

err_vhost_dev:
    /* vhost_dev_cleanup() closes the vhostfd passed to vhost_dev_init() */
    vhost_dev_cleanup(&vvc->vhost_dev);
err_virtio:
    vhost_vsock_common_unrealize(vdev);
}

static void vhost_vsock_device_unrealize(DeviceState *dev)
{
    VHostVSockCommon *vvc = VHOST_VSOCK_COMMON(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    /* This will stop vhost backend if appropriate. */
    vhost_vsock_set_status(vdev, 0);

    vhost_dev_cleanup(&vvc->vhost_dev);
    vhost_vsock_common_unrealize(vdev);
}

static const Property vhost_vsock_properties[] = {
    DEFINE_PROP_UINT64("guest-cid", VHostVSock, conf.guest_cid, 0),
    DEFINE_PROP_STRING("vhostfd", VHostVSock, conf.vhostfd),
};

static void vhost_vsock_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_vsock_properties);
    dc->vmsd = &vmstate_virtio_vhost_vsock;
    vdc->realize = vhost_vsock_device_realize;
    vdc->unrealize = vhost_vsock_device_unrealize;
    vdc->get_features = vhost_vsock_get_features;
    vdc->get_config = vhost_vsock_get_config;
    vdc->set_status = vhost_vsock_set_status;
}

static const TypeInfo vhost_vsock_info = {
    .name = TYPE_VHOST_VSOCK,
    .parent = TYPE_VHOST_VSOCK_COMMON,
    .instance_size = sizeof(VHostVSock),
    .class_init = vhost_vsock_class_init,
};

static void vhost_vsock_register_types(void)
{
    type_register_static(&vhost_vsock_info);
}

type_init(vhost_vsock_register_types)
