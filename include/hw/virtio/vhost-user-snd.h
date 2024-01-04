/*
 * Vhost-user Sound virtio device
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_SND_H
#define QEMU_VHOST_USER_SND_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-user-base.h"

#define TYPE_VHOST_USER_SND "vhost-user-snd"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserSound, VHOST_USER_SND)

struct VHostUserSound {
    VHostUserBase parent_obj;
};

#endif /* QEMU_VHOST_USER_SND_H */
