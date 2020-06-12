/*
 * Vhost vsock virtio device
 *
 * Copyright 2015 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VHOST_VSOCK_H
#define QEMU_VHOST_VSOCK_H

#include "hw/virtio/vhost-vsock-common.h"

#define TYPE_VHOST_VSOCK "vhost-vsock-device"
#define VHOST_VSOCK(obj) \
        OBJECT_CHECK(VHostVSock, (obj), TYPE_VHOST_VSOCK)

typedef struct {
    uint64_t guest_cid;
    char *vhostfd;
} VHostVSockConf;

typedef struct {
    /*< private >*/
    VHostVSockCommon parent;
    VHostVSockConf conf;

    /*< public >*/
} VHostVSock;

#endif /* QEMU_VHOST_VSOCK_H */
