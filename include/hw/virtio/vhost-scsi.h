/*
 * vhost_scsi host device
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_SCSI_H
#define VHOST_SCSI_H

#include "qemu-common.h"
#include "hw/qdev.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/vhost.h"

/*
 * Used by QEMU userspace to ensure a consistent vhost-scsi ABI.
 *
 * ABI Rev 0: July 2012 version starting point for v3.6-rc merge candidate +
 *            RFC-v2 vhost-scsi userspace.  Add GET_ABI_VERSION ioctl usage
 * ABI Rev 1: January 2013. Ignore vhost_tpgt filed in struct vhost_scsi_target.
 * 	      All the targets under vhost_wwpn can be seen and used by guest.
 */

#define VHOST_SCSI_ABI_VERSION 1

/* TODO #include <linux/vhost.h> properly */
/* For VHOST_SCSI_SET_ENDPOINT/VHOST_SCSI_CLEAR_ENDPOINT ioctl */
struct vhost_scsi_target {
    int abi_version;
    char vhost_wwpn[224];
    unsigned short vhost_tpgt;
    unsigned short reserved;
};

enum vhost_scsi_vq_list {
    VHOST_SCSI_VQ_CONTROL = 0,
    VHOST_SCSI_VQ_EVENT = 1,
    VHOST_SCSI_VQ_NUM_FIXED = 2,
};

#define VHOST_VIRTIO 0xAF
#define VHOST_SCSI_SET_ENDPOINT _IOW(VHOST_VIRTIO, 0x40, struct vhost_scsi_target)
#define VHOST_SCSI_CLEAR_ENDPOINT _IOW(VHOST_VIRTIO, 0x41, struct vhost_scsi_target)
#define VHOST_SCSI_GET_ABI_VERSION _IOW(VHOST_VIRTIO, 0x42, int)

#define TYPE_VHOST_SCSI "vhost-scsi"
#define VHOST_SCSI(obj) \
        OBJECT_CHECK(VHostSCSI, (obj), TYPE_VHOST_SCSI)

typedef struct VHostSCSI {
    VirtIOSCSICommon parent_obj;

    Error *migration_blocker;

    struct vhost_dev dev;
} VHostSCSI;

#define DEFINE_VHOST_SCSI_PROPERTIES(_state, _conf_field) \
    DEFINE_PROP_STRING("vhostfd", _state, _conf_field.vhostfd), \
    DEFINE_PROP_STRING("wwpn", _state, _conf_field.wwpn), \
    DEFINE_PROP_UINT32("num_queues", _state, _conf_field.num_queues, 1), \
    DEFINE_PROP_UINT32("max_sectors", _state, _conf_field.max_sectors, 0xFFFF), \
    DEFINE_PROP_UINT32("cmd_per_lun", _state, _conf_field.cmd_per_lun, 128)


#endif
