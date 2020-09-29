/*
 * vhost-user-blk host device
 * Copyright(C) 2017 Intel Corporation.
 *
 * Authors:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * Based on vhost-scsi.h, Copyright IBM, Corp. 2011
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef VHOST_USER_BLK_H
#define VHOST_USER_BLK_H

#include "standard-headers/linux/virtio_blk.h"
#include "hw/block/block.h"
#include "chardev/char-fe.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_BLK "vhost-user-blk"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserBlk, VHOST_USER_BLK)

#define VHOST_USER_BLK_AUTO_NUM_QUEUES UINT16_MAX

struct VHostUserBlk {
    VirtIODevice parent_obj;
    CharBackend chardev;
    int32_t bootindex;
    struct virtio_blk_config blkcfg;
    uint16_t num_queues;
    uint32_t queue_size;
    uint32_t config_wce;
    struct vhost_dev dev;
    struct vhost_inflight *inflight;
    VhostUserState vhost_user;
    struct vhost_virtqueue *vhost_vqs;
    VirtQueue **virtqs;

    /*
     * There are at least two steps of initialization of the
     * vhost-user device. The first is a "connect" step and
     * second is a "start" step. Make a separation between
     * those initialization phases by using two fields.
     */
    /* vhost_user_blk_connect/vhost_user_blk_disconnect */
    bool connected;
    /* vhost_user_blk_start/vhost_user_blk_stop */
    bool started_vu;
};

#endif
