/*
 * vhost_scsi host device
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 *
 * Author:
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_SCSI_COMMON_H
#define VHOST_SCSI_COMMON_H

#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/vhost.h"
#include "hw/fw-path-provider.h"

#define TYPE_VHOST_SCSI_COMMON "vhost-scsi-common"
#define VHOST_SCSI_COMMON(obj) \
        OBJECT_CHECK(VHostSCSICommon, (obj), TYPE_VHOST_SCSI_COMMON)

typedef struct VHostSCSICommon {
    VirtIOSCSICommon parent_obj;

    Error *migration_blocker;

    struct vhost_dev dev;
    const int *feature_bits;
    int32_t bootindex;
    int channel;
    int target;
    int lun;
    uint64_t host_features;
    bool migratable;
} VHostSCSICommon;

int vhost_scsi_common_start(VHostSCSICommon *vsc);
void vhost_scsi_common_stop(VHostSCSICommon *vsc);
char *vhost_scsi_common_get_fw_dev_path(FWPathProvider *p, BusState *bus,
                                        DeviceState *dev);
void vhost_scsi_common_set_config(VirtIODevice *vdev, const uint8_t *config);
uint64_t vhost_scsi_common_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp);

#endif /* VHOST_SCSI_COMMON_H */
