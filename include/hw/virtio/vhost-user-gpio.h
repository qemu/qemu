/*
 * Vhost-user GPIO virtio device
 *
 * Copyright (c) 2021 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _QEMU_VHOST_USER_GPIO_H
#define _QEMU_VHOST_USER_GPIO_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-user-base.h"

#define TYPE_VHOST_USER_GPIO "vhost-user-gpio-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserGPIO, VHOST_USER_GPIO);

struct VHostUserGPIO {
    VHostUserBase parent_obj;
};

#endif /* _QEMU_VHOST_USER_GPIO_H */
