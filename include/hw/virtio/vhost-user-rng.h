/*
 * Vhost-user RNG virtio device
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_RNG_H
#define QEMU_VHOST_USER_RNG_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-user-base.h"

#define TYPE_VHOST_USER_RNG "vhost-user-rng"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserRNG, VHOST_USER_RNG)

struct VHostUserRNG {
    VHostUserBase parent_obj;
};

#endif /* QEMU_VHOST_USER_RNG_H */
