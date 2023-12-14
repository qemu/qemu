/*
 * Vhost-user SCMI virtio device
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _QEMU_VHOST_USER_SCMI_H
#define _QEMU_VHOST_USER_SCMI_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

#define TYPE_VHOST_USER_SCMI "vhost-user-scmi"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserSCMI, VHOST_USER_SCMI);

struct VHostUserSCMI {
    VirtIODevice parent;
    CharBackend chardev;
    struct vhost_virtqueue *vhost_vqs;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *cmd_vq;
    VirtQueue *event_vq;
    bool connected;
    bool started_vu;
};

#endif /* _QEMU_VHOST_USER_SCMI_H */
