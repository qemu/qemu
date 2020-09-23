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

#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-scsi-common.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_SCSI "vhost-user-scsi"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserSCSI, VHOST_USER_SCSI)

struct VHostUserSCSI {
    VHostSCSICommon parent_obj;
    VhostUserState vhost_user;
};

#endif /* VHOST_USER_SCSI_H */
