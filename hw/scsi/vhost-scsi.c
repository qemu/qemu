/*
 * vhost_scsi host device
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * Changes for QEMU mainline + tcm_vhost kernel upstream:
 *  Nicholas Bellinger <nab@risingtidesystems.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <linux/vhost.h>
#include <sys/ioctl.h>
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/queue.h"
#include "monitor/monitor.h"
#include "migration/blocker.h"
#include "hw/virtio/vhost-scsi.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/fw-path-provider.h"
#include "qemu/cutils.h"

/* Features supported by host kernel. */
static const int kernel_feature_bits[] = {
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_SCSI_F_HOTPLUG,
    VHOST_INVALID_FEATURE_BIT
};

static int vhost_scsi_set_endpoint(VHostSCSI *s)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    const VhostOps *vhost_ops = vsc->dev.vhost_ops;
    struct vhost_scsi_target backend;
    int ret;

    memset(&backend, 0, sizeof(backend));
    pstrcpy(backend.vhost_wwpn, sizeof(backend.vhost_wwpn), vs->conf.wwpn);
    ret = vhost_ops->vhost_scsi_set_endpoint(&vsc->dev, &backend);
    if (ret < 0) {
        return -errno;
    }
    return 0;
}

static void vhost_scsi_clear_endpoint(VHostSCSI *s)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(s);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    struct vhost_scsi_target backend;
    const VhostOps *vhost_ops = vsc->dev.vhost_ops;

    memset(&backend, 0, sizeof(backend));
    pstrcpy(backend.vhost_wwpn, sizeof(backend.vhost_wwpn), vs->conf.wwpn);
    vhost_ops->vhost_scsi_clear_endpoint(&vsc->dev, &backend);
}

static int vhost_scsi_start(VHostSCSI *s)
{
    int ret, abi_version;
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    const VhostOps *vhost_ops = vsc->dev.vhost_ops;

    ret = vhost_ops->vhost_scsi_get_abi_version(&vsc->dev, &abi_version);
    if (ret < 0) {
        return -errno;
    }
    if (abi_version > VHOST_SCSI_ABI_VERSION) {
        error_report("vhost-scsi: The running tcm_vhost kernel abi_version:"
                     " %d is greater than vhost_scsi userspace supports: %d,"
                     " please upgrade your version of QEMU", abi_version,
                     VHOST_SCSI_ABI_VERSION);
        return -ENOSYS;
    }

    ret = vhost_scsi_common_start(vsc);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_scsi_set_endpoint(s);
    if (ret < 0) {
        error_report("Error setting vhost-scsi endpoint");
        vhost_scsi_common_stop(vsc);
    }

    return ret;
}

static void vhost_scsi_stop(VHostSCSI *s)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);

    vhost_scsi_clear_endpoint(s);
    vhost_scsi_common_stop(vsc);
}

static void vhost_scsi_set_status(VirtIODevice *vdev, uint8_t val)
{
    VHostSCSI *s = VHOST_SCSI(vdev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(s);
    bool start = (val & VIRTIO_CONFIG_S_DRIVER_OK);

    if (!vdev->vm_running) {
        start = false;
    }

    if (vsc->dev.started == start) {
        return;
    }

    if (start) {
        int ret;

        ret = vhost_scsi_start(s);
        if (ret < 0) {
            error_report("unable to start vhost-scsi: %s", strerror(-ret));
            exit(1);
        }
    } else {
        vhost_scsi_stop(s);
    }
}

static void vhost_dummy_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
}

static int vhost_scsi_pre_save(void *opaque)
{
    VHostSCSICommon *vsc = opaque;

    /* At this point, backend must be stopped, otherwise
     * it might keep writing to memory. */
    assert(!vsc->dev.started);

    return 0;
}

static const VMStateDescription vmstate_virtio_vhost_scsi = {
    .name = "virtio-vhost_scsi",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
    .pre_save = vhost_scsi_pre_save,
};

static void vhost_scsi_realize(DeviceState *dev, Error **errp)
{
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(dev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(dev);
    Error *err = NULL;
    int vhostfd = -1;
    int ret;

    if (!vs->conf.wwpn) {
        error_setg(errp, "vhost-scsi: missing wwpn");
        return;
    }

    if (vs->conf.vhostfd) {
        vhostfd = monitor_fd_param(cur_mon, vs->conf.vhostfd, errp);
        if (vhostfd == -1) {
            error_prepend(errp, "vhost-scsi: unable to parse vhostfd: ");
            return;
        }
    } else {
        vhostfd = open("/dev/vhost-scsi", O_RDWR);
        if (vhostfd < 0) {
            error_setg(errp, "vhost-scsi: open vhost char device failed: %s",
                       strerror(errno));
            return;
        }
    }

    virtio_scsi_common_realize(dev,
                               vhost_dummy_handle_output,
                               vhost_dummy_handle_output,
                               vhost_dummy_handle_output,
                               &err);
    if (err != NULL) {
        error_propagate(errp, err);
        goto close_fd;
    }

    if (!vsc->migratable) {
        error_setg(&vsc->migration_blocker,
                "vhost-scsi does not support migration in all cases. "
                "When external environment supports it (Orchestrator migrates "
                "target SCSI device state or use shared storage over network), "
                "set 'migratable' property to true to enable migration.");
        migrate_add_blocker(vsc->migration_blocker, &err);
        if (err) {
            error_propagate(errp, err);
            error_free(vsc->migration_blocker);
            goto close_fd;
        }
    }

    vsc->dev.nvqs = VHOST_SCSI_VQ_NUM_FIXED + vs->conf.num_queues;
    vsc->dev.vqs = g_new0(struct vhost_virtqueue, vsc->dev.nvqs);
    vsc->dev.vq_index = 0;
    vsc->dev.backend_features = 0;

    ret = vhost_dev_init(&vsc->dev, (void *)(uintptr_t)vhostfd,
                         VHOST_BACKEND_TYPE_KERNEL, 0);
    if (ret < 0) {
        error_setg(errp, "vhost-scsi: vhost initialization failed: %s",
                   strerror(-ret));
        goto free_vqs;
    }

    /* At present, channel and lun both are 0 for bootable vhost-scsi disk */
    vsc->channel = 0;
    vsc->lun = 0;
    /* Note: we can also get the minimum tpgt from kernel */
    vsc->target = vs->conf.boot_tpgt;

    return;

 free_vqs:
    if (!vsc->migratable) {
        migrate_del_blocker(vsc->migration_blocker);
    }
    g_free(vsc->dev.vqs);
 close_fd:
    close(vhostfd);
    return;
}

static void vhost_scsi_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(dev);
    struct vhost_virtqueue *vqs = vsc->dev.vqs;

    if (!vsc->migratable) {
        migrate_del_blocker(vsc->migration_blocker);
        error_free(vsc->migration_blocker);
    }

    /* This will stop vhost backend. */
    vhost_scsi_set_status(vdev, 0);

    vhost_dev_cleanup(&vsc->dev);
    g_free(vqs);

    virtio_scsi_common_unrealize(dev, errp);
}

static Property vhost_scsi_properties[] = {
    DEFINE_PROP_STRING("vhostfd", VirtIOSCSICommon, conf.vhostfd),
    DEFINE_PROP_STRING("wwpn", VirtIOSCSICommon, conf.wwpn),
    DEFINE_PROP_UINT32("boot_tpgt", VirtIOSCSICommon, conf.boot_tpgt, 0),
    DEFINE_PROP_UINT32("num_queues", VirtIOSCSICommon, conf.num_queues, 1),
    DEFINE_PROP_UINT32("virtqueue_size", VirtIOSCSICommon, conf.virtqueue_size,
                       128),
    DEFINE_PROP_UINT32("max_sectors", VirtIOSCSICommon, conf.max_sectors,
                       0xFFFF),
    DEFINE_PROP_UINT32("cmd_per_lun", VirtIOSCSICommon, conf.cmd_per_lun, 128),
    DEFINE_PROP_BIT64("t10_pi", VHostSCSICommon, host_features,
                                                 VIRTIO_SCSI_F_T10_PI,
                                                 false),
    DEFINE_PROP_BOOL("migratable", VHostSCSICommon, migratable, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    FWPathProviderClass *fwc = FW_PATH_PROVIDER_CLASS(klass);

    dc->props = vhost_scsi_properties;
    dc->vmsd = &vmstate_virtio_vhost_scsi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = vhost_scsi_realize;
    vdc->unrealize = vhost_scsi_unrealize;
    vdc->get_features = vhost_scsi_common_get_features;
    vdc->set_config = vhost_scsi_common_set_config;
    vdc->set_status = vhost_scsi_set_status;
    fwc->get_dev_path = vhost_scsi_common_get_fw_dev_path;
}

static void vhost_scsi_instance_init(Object *obj)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(obj);

    vsc->feature_bits = kernel_feature_bits;

    device_add_bootindex_property(obj, &vsc->bootindex, "bootindex", NULL,
                                  DEVICE(vsc), NULL);
}

static const TypeInfo vhost_scsi_info = {
    .name = TYPE_VHOST_SCSI,
    .parent = TYPE_VHOST_SCSI_COMMON,
    .instance_size = sizeof(VHostSCSI),
    .class_init = vhost_scsi_class_init,
    .instance_init = vhost_scsi_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FW_PATH_PROVIDER },
        { }
    },
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_scsi_info);
}

type_init(virtio_register_types)
