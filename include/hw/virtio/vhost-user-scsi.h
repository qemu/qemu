/*
 * vhost-user-scsi host device
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 *
 * Author:
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This file is largely based on "vhost-scsi.h" by:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_USER_SCSI_H
#define VHOST_USER_SCSI_H

#include "hw/qdev.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-scsi-common.h"

#define TYPE_VHOST_USER_SCSI "vhost-user-scsi"
#define VHOST_USER_SCSI(obj) \
        OBJECT_CHECK(VHostUserSCSI, (obj), TYPE_VHOST_USER_SCSI)

typedef struct VHostUserSCSI {
    VHostSCSICommon parent_obj;
    VhostUserState vhost_user;
} VHostUserSCSI;

#endif /* VHOST_USER_SCSI_H */
