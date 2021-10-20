/*
 * Vhost-user RNG virtio device
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _QEMU_VHOST_USER_RNG_H
#define _QEMU_VHOST_USER_RNG_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_USER_RNG "vhost-user-rng"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserRNG, VHOST_USER_RNG)

struct VHostUserRNG {
    /*< private >*/
    VirtIODevice parent;
    CharBackend chardev;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *req_vq;
    bool connected;

    /*< public >*/
};

#endif /* _QEMU_VHOST_USER_RNG_H */
