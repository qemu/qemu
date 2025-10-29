/*
 * Vhost-user generic virtio device
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VHOST_USER_BASE_H
#define QEMU_VHOST_USER_BASE_H

#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

#define TYPE_VHOST_USER_BASE "vhost-user-base"

OBJECT_DECLARE_TYPE(VHostUserBase, VHostUserBaseClass, VHOST_USER_BASE)

struct VHostUserBase {
    VirtIODevice parent_obj;

    /* Properties */
    CharFrontend chardev;
    uint16_t virtio_id;
    uint32_t num_vqs;
    uint32_t vq_size; /* can't exceed VIRTIO_QUEUE_MAX */
    uint32_t config_size;
    /* State tracking */
    VhostUserState vhost_user;
    struct vhost_virtqueue *vhost_vq;
    struct vhost_dev vhost_dev;
    GPtrArray *vqs;
    bool connected;
};

/*
 * Needed so we can use the base realize after specialisation
 * tweaks
 */
struct VHostUserBaseClass {
    VirtioDeviceClass parent_class;

    DeviceRealize parent_realize;
};


#define TYPE_VHOST_USER_TEST_DEVICE "vhost-user-test-device"

#endif /* QEMU_VHOST_USER_BASE_H */
