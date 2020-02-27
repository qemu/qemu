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

#ifndef _QEMU_VHOST_USER_FS_H
#define _QEMU_VHOST_USER_FS_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_USER_FS "vhost-user-fs-device"
#define VHOST_USER_FS(obj) \
        OBJECT_CHECK(VHostUserFS, (obj), TYPE_VHOST_USER_FS)

typedef struct {
    CharBackend chardev;
    char *tag;
    uint16_t num_request_queues;
    uint16_t queue_size;
} VHostUserFSConf;

typedef struct {
    /*< private >*/
    VirtIODevice parent;
    VHostUserFSConf conf;
    struct vhost_virtqueue *vhost_vqs;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue **req_vqs;
    VirtQueue *hiprio_vq;

    /*< public >*/
} VHostUserFS;

#endif /* _QEMU_VHOST_USER_FS_H */
