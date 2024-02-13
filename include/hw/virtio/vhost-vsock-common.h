/*
 * Parent class for vhost-vsock devices
 *
 * Copyright 2015-2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VHOST_VSOCK_COMMON_H
#define QEMU_VHOST_VSOCK_COMMON_H

#include "qapi/qapi-types-common.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "qom/object.h"

#define TYPE_VHOST_VSOCK_COMMON "vhost-vsock-common"
OBJECT_DECLARE_SIMPLE_TYPE(VHostVSockCommon, VHOST_VSOCK_COMMON)

enum {
    VHOST_VSOCK_SAVEVM_VERSION = 0,

    VHOST_VSOCK_QUEUE_SIZE = 128,
};

struct VHostVSockCommon {
    VirtIODevice parent;

    struct vhost_virtqueue vhost_vqs[2];
    struct vhost_dev vhost_dev;

    VirtQueue *event_vq;
    VirtQueue *recv_vq;
    VirtQueue *trans_vq;

    QEMUTimer *post_load_timer;

    /* features */
    OnOffAuto seqpacket;
};

int vhost_vsock_common_start(VirtIODevice *vdev);
void vhost_vsock_common_stop(VirtIODevice *vdev);
int vhost_vsock_common_pre_save(void *opaque);
int vhost_vsock_common_post_load(void *opaque, int version_id);
void vhost_vsock_common_realize(VirtIODevice *vdev);
void vhost_vsock_common_unrealize(VirtIODevice *vdev);
uint64_t vhost_vsock_common_get_features(VirtIODevice *vdev, uint64_t features,
                                         Error **errp);

#endif /* QEMU_VHOST_VSOCK_COMMON_H */
