/*
 * Vhost-user filesystem virtio device
 *
 * Copyright 2018-2019 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VHOST_USER_FS_H
#define QEMU_VHOST_USER_FS_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_FS "vhost-user-fs-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserFS, VHOST_USER_FS)

typedef struct {
    CharBackend chardev;
    char *tag;
    uint16_t num_request_queues;
    uint16_t queue_size;
} VHostUserFSConf;

struct VHostUserFS {
    /*< private >*/
    VirtIODevice parent;
    VHostUserFSConf conf;
    struct vhost_virtqueue *vhost_vqs;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue **req_vqs;
    VirtQueue *hiprio_vq;
    int32_t bootindex;

    /*< public >*/
};

#endif /* QEMU_VHOST_USER_FS_H */
