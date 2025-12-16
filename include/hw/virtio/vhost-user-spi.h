/*
 * Vhost-user spi virtio device
 *
 * Copyright (C) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_SPI_H
#define QEMU_VHOST_USER_SPI_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-user-base.h"

#define TYPE_VHOST_USER_SPI "vhost-user-spi-device"

OBJECT_DECLARE_SIMPLE_TYPE(VHostUserSPI, VHOST_USER_SPI)

struct VHostUserSPI {
    VHostUserBase parent_obj;
};

#endif /* QEMU_VHOST_USER_SPI_H */
