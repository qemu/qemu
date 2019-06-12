/*
 * vhost-scsi-common
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
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-scsi-common.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/fw-path-provider.h"

int vhost_scsi_common_start(VHostSCSICommon *vsc)
{
    int ret, i;
    VirtIODevice *vdev = VIRTIO_DEVICE(vsc);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&vsc->dev, vdev);
    if (ret < 0) {
        return ret;
    }

    ret = k->set_guest_notifiers(qbus->parent, vsc->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier");
        goto err_host_notifiers;
    }

    vsc->dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&vsc->dev, vdev);
    if (ret < 0) {
        error_report("Error start vhost dev");
        goto err_guest_notifiers;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < vsc->dev.nvqs; i++) {
        vhost_virtqueue_mask(&vsc->dev, vdev, vsc->dev.vq_index + i, false);
    }

    return ret;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, vsc->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&vsc->dev, vdev);
    return ret;
}

void vhost_scsi_common_stop(VHostSCSICommon *vsc)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vsc);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret = 0;

    vhost_dev_stop(&vsc->dev, vdev);

    if (k->set_guest_notifiers) {
        ret = k->set_guest_notifiers(qbus->parent, vsc->dev.nvqs, false);
        if (ret < 0) {
                error_report("vhost guest notifier cleanup failed: %d", ret);
        }
    }
    assert(ret >= 0);

    vhost_dev_disable_notifiers(&vsc->dev, vdev);
}

uint64_t vhost_scsi_common_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(vdev);

    /* Turn on predefined features supported by this device */
    features |= vsc->host_features;

    return vhost_get_features(&vsc->dev, vsc->feature_bits, features);
}

void vhost_scsi_common_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIOSCSIConfig *scsiconf = (VirtIOSCSIConfig *)config;
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if ((uint32_t)virtio_ldl_p(vdev, &scsiconf->sense_size) != vs->sense_size ||
        (uint32_t)virtio_ldl_p(vdev, &scsiconf->cdb_size) != vs->cdb_size) {
        error_report("vhost-scsi does not support changing the sense data and "
                     "CDB sizes");
        exit(1);
    }
}

/*
 * Implementation of an interface to adjust firmware path
 * for the bootindex property handling.
 */
char *vhost_scsi_common_get_fw_dev_path(FWPathProvider *p, BusState *bus,
                                        DeviceState *dev)
{
    VHostSCSICommon *vsc = VHOST_SCSI_COMMON(dev);
    /* format: /channel@channel/vhost-scsi@target,lun */
    return g_strdup_printf("/channel@%x/%s@%x,%x", vsc->channel,
                           qdev_fw_name(dev), vsc->target, vsc->lun);
}

static const TypeInfo vhost_scsi_common_info = {
    .name = TYPE_VHOST_SCSI_COMMON,
    .parent = TYPE_VIRTIO_SCSI_COMMON,
    .instance_size = sizeof(VHostSCSICommon),
    .abstract = true,
};

static void virtio_register_types(void)
{
    type_register_static(&vhost_scsi_common_info);
}

type_init(virtio_register_types)
