/*
 * Vhost-user i2c virtio device
 *
 * Copyright (c) 2021 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _QEMU_VHOST_USER_I2C_H
#define _QEMU_VHOST_USER_I2C_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

#define TYPE_VHOST_USER_I2C "vhost-user-i2c-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserI2C, VHOST_USER_I2C)

struct VHostUserI2C {
    VirtIODevice parent;
    CharBackend chardev;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *vq;
    bool connected;
};

/* Virtio Feature bits */
#define VIRTIO_I2C_F_ZERO_LENGTH_REQUEST		0

#endif /* _QEMU_VHOST_USER_I2C_H */
