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
#include "standard-headers/linux/virtio_gpio.h"
#include "chardev/char-fe.h"

#define TYPE_VHOST_USER_GPIO "vhost-user-gpio-device"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserGPIO, VHOST_USER_GPIO);

struct VHostUserGPIO {
    /*< private >*/
    VirtIODevice parent_obj;
    CharBackend chardev;
    struct virtio_gpio_config config;
    struct vhost_virtqueue *vhost_vqs;
    struct vhost_dev vhost_dev;
    VhostUserState vhost_user;
    VirtQueue *command_vq;
    VirtQueue *interrupt_vq;
    /**
     * There are at least two steps of initialization of the
     * vhost-user device. The first is a "connect" step and
     * second is a "start" step. Make a separation between
     * those initialization phases by using two fields.
     *
     * @connected: see vu_gpio_connect()/vu_gpio_disconnect()
     * @started_vu: see vu_gpio_start()/vu_gpio_stop()
     */
    bool connected;
    bool started_vu;
    /*< public >*/
};

#endif /* _QEMU_VHOST_USER_GPIO_H */
