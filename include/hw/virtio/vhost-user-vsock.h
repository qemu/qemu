/*
 * Vhost-user vsock virtio device
 *
 * Copyright 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef _QEMU_VHOST_USER_VSOCK_H
#define _QEMU_VHOST_USER_VSOCK_H

#include "hw/virtio/vhost-vsock-common.h"
#include "hw/virtio/vhost-user.h"
#include "standard-headers/linux/virtio_vsock.h"
#include "qom/object.h"

#define TYPE_VHOST_USER_VSOCK "vhost-user-vsock-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserVSock, VHOST_USER_VSOCK)

typedef struct {
    CharBackend chardev;
} VHostUserVSockConf;

struct VHostUserVSock {
    /*< private >*/
    VHostVSockCommon parent;
    VhostUserState vhost_user;
    VHostUserVSockConf conf;
    struct virtio_vsock_config vsockcfg;

    /*< public >*/
};

#endif /* _QEMU_VHOST_USER_VSOCK_H */
