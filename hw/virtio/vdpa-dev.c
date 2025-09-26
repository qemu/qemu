/*
 * Vhost Vdpa Device
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2022. All Rights Reserved.
 *
 * Authors:
 *   Longpeng <longpeng2@huawei.com>
 *
 * Largely based on the "vhost-user-blk-pci.c" and "vhost-user-blk.c"
 * implemented by:
 *   Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vhost.h>
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/vdpa-dev.h"
#include "system/system.h"
#include "system/runstate.h"

static void
vhost_vdpa_device_dummy_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /* Nothing to do */
}

static uint32_t
vhost_vdpa_device_get_u32(int fd, unsigned long int cmd, Error **errp)
{
    uint32_t val = (uint32_t)-1;

    if (ioctl(fd, cmd, &val) < 0) {
        error_setg(errp, "vhost-vdpa-device: cmd 0x%lx failed: %s",
                   cmd, strerror(errno));
    }

    return val;
}

static void vhost_vdpa_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostVdpaDevice *v = VHOST_VDPA_DEVICE(vdev);
    struct vhost_vdpa_iova_range iova_range;
    uint16_t max_queue_size;
    struct vhost_virtqueue *vqs;
    int i, ret;

    if (!v->vhostdev) {
        error_setg(errp, "vhost-vdpa-device: vhostdev are missing");
        return;
    }

    v->vhostfd = qemu_open(v->vhostdev, O_RDWR, errp);
    if (*errp) {
        return;
    }

    v->vdev_id = vhost_vdpa_device_get_u32(v->vhostfd,
                                           VHOST_VDPA_GET_DEVICE_ID, errp);
    if (*errp) {
        goto out;
    }

    max_queue_size = vhost_vdpa_device_get_u32(v->vhostfd,
                                               VHOST_VDPA_GET_VRING_NUM, errp);
    if (*errp) {
        goto out;
    }

    if (v->queue_size > max_queue_size) {
        error_setg(errp, "vhost-vdpa-device: invalid queue_size: %u (max:%u)",
                   v->queue_size, max_queue_size);
        goto out;
    } else if (!v->queue_size) {
        v->queue_size = max_queue_size;
    }

    v->num_queues = vhost_vdpa_device_get_u32(v->vhostfd,
                                              VHOST_VDPA_GET_VQS_COUNT, errp);
    if (*errp) {
        goto out;
    }

    if (!v->num_queues || v->num_queues > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "invalid number of virtqueues: %u (max:%u)",
                   v->num_queues, VIRTIO_QUEUE_MAX);
        goto out;
    }

    v->dev.nvqs = v->num_queues;
    vqs = g_new0(struct vhost_virtqueue, v->dev.nvqs);
    v->dev.vqs = vqs;
    v->dev.vq_index = 0;
    v->dev.vq_index_end = v->dev.nvqs;
    v->dev.backend_features = 0;
    v->started = false;

    ret = vhost_vdpa_get_iova_range(v->vhostfd, &iova_range);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device: get iova range failed: %s",
                   strerror(-ret));
        goto free_vqs;
    }
    v->vdpa.shared = g_new0(VhostVDPAShared, 1);
    v->vdpa.shared->device_fd = v->vhostfd;
    v->vdpa.shared->iova_range = iova_range;

    ret = vhost_dev_init(&v->dev, &v->vdpa, VHOST_BACKEND_TYPE_VDPA, 0, NULL);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device: vhost initialization failed: %s",
                   strerror(-ret));
        goto free_vqs;
    }

    v->config_size = vhost_vdpa_device_get_u32(v->vhostfd,
                                               VHOST_VDPA_GET_CONFIG_SIZE,
                                               errp);
    if (*errp) {
        goto vhost_cleanup;
    }

    /*
     * Invoke .post_init() to initialize the transport-specific fields
     * before calling virtio_init().
     */
    if (v->post_init && v->post_init(v, errp) < 0) {
        goto vhost_cleanup;
    }

    v->config = g_malloc0(v->config_size);

    ret = vhost_dev_get_config(&v->dev, v->config, v->config_size, NULL);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device: get config failed");
        goto free_config;
    }

    virtio_init(vdev, v->vdev_id, v->config_size);

    v->virtqs = g_new0(VirtQueue *, v->dev.nvqs);
    for (i = 0; i < v->dev.nvqs; i++) {
        v->virtqs[i] = virtio_add_queue(vdev, v->queue_size,
                                        vhost_vdpa_device_dummy_handle_output);
    }

    return;

free_config:
    g_free(v->config);
vhost_cleanup:
    vhost_dev_cleanup(&v->dev);
free_vqs:
    g_free(vqs);
    g_free(v->vdpa.shared);
out:
    qemu_close(v->vhostfd);
    v->vhostfd = -1;
}

static void vhost_vdpa_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    int i;

    virtio_set_status(vdev, 0);

    for (i = 0; i < s->num_queues; i++) {
        virtio_delete_queue(s->virtqs[i]);
    }
    g_free(s->virtqs);
    virtio_cleanup(vdev);

    g_free(s->config);
    g_free(s->dev.vqs);
    vhost_dev_cleanup(&s->dev);
    g_free(s->vdpa.shared);
    qemu_close(s->vhostfd);
    s->vhostfd = -1;
}

static void
vhost_vdpa_device_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    int ret;

    ret = vhost_dev_get_config(&s->dev, s->config, s->config_size,
                            NULL);
    if (ret < 0) {
        error_report("get device config space failed");
        return;
    }
    memcpy(config, s->config, s->config_size);
}

static void
vhost_vdpa_device_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    int ret;

    ret = vhost_dev_set_config(&s->dev, s->config, 0, s->config_size,
                               VHOST_SET_CONFIG_TYPE_FRONTEND);
    if (ret) {
        error_report("set device config space failed");
        return;
    }
}

static uint64_t vhost_vdpa_device_get_features(VirtIODevice *vdev,
                                               uint64_t features,
                                               Error **errp)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    uint64_t backend_features = s->dev.features;

    if (!virtio_has_feature(features, VIRTIO_F_IOMMU_PLATFORM)) {
        virtio_clear_feature(&backend_features, VIRTIO_F_IOMMU_PLATFORM);
    }

    return backend_features;
}

static int vhost_vdpa_device_start(VirtIODevice *vdev, Error **errp)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
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

    ret = vhost_dev_start(&s->dev, vdev, true);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Error starting vhost");
        goto err_guest_notifiers;
    }
    s->started = true;

    /*
     * guest_notifier_mask/pending not used yet, so just unmask
     * everything here. virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < s->dev.nvqs; i++) {
        vhost_virtqueue_mask(&s->dev, vdev, i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&s->dev, vdev);
    return ret;
}

static void vhost_vdpa_device_stop(VirtIODevice *vdev)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret;

    if (!s->started) {
        return;
    }
    s->started = false;

    if (!k->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&s->dev, vdev, false);

    ret = k->set_guest_notifiers(qbus->parent, s->dev.nvqs, false);
    if (ret < 0) {
        error_report("vhost guest notifier cleanup failed: %d", ret);
        return;
    }

    vhost_dev_disable_notifiers(&s->dev, vdev);
}

static int vhost_vdpa_device_set_status(VirtIODevice *vdev, uint8_t status)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    bool should_start = virtio_device_started(vdev, status);
    Error *local_err = NULL;
    int ret;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (s->started == should_start) {
        return 0;
    }

    if (should_start) {
        ret = vhost_vdpa_device_start(vdev, &local_err);
        if (ret < 0) {
            error_reportf_err(local_err, "vhost-vdpa-device: start failed: ");
        }
    } else {
        vhost_vdpa_device_stop(vdev);
    }
    return 0;
}

static struct vhost_dev *vhost_vdpa_device_get_vhost(VirtIODevice *vdev)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    return &s->dev;
}

static const Property vhost_vdpa_device_properties[] = {
    DEFINE_PROP_STRING("vhostdev", VhostVdpaDevice, vhostdev),
    DEFINE_PROP_UINT16("queue-size", VhostVdpaDevice, queue_size, 0),
};

static const VMStateDescription vmstate_vhost_vdpa_device = {
    .name = "vhost-vdpa-device",
    .unmigratable = 1,
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void vhost_vdpa_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_vdpa_device_properties);
    dc->desc = "VDPA-based generic device assignment";
    dc->vmsd = &vmstate_vhost_vdpa_device;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = vhost_vdpa_device_realize;
    vdc->unrealize = vhost_vdpa_device_unrealize;
    vdc->get_config = vhost_vdpa_device_get_config;
    vdc->set_config = vhost_vdpa_device_set_config;
    vdc->get_features = vhost_vdpa_device_get_features;
    vdc->set_status = vhost_vdpa_device_set_status;
    vdc->get_vhost = vhost_vdpa_device_get_vhost;
}

static void vhost_vdpa_device_instance_init(Object *obj)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  NULL, DEVICE(obj));
}

static const TypeInfo vhost_vdpa_device_info = {
    .name = TYPE_VHOST_VDPA_DEVICE,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VhostVdpaDevice),
    .class_init = vhost_vdpa_device_class_init,
    .instance_init = vhost_vdpa_device_instance_init,
};

static void register_vhost_vdpa_device_type(void)
{
    type_register_static(&vhost_vdpa_device_info);
}

type_init(register_vhost_vdpa_device_type);
