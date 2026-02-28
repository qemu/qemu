/*
 * Virtio RTC device
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VIRTIO_RTC_H
#define QEMU_VIRTIO_RTC_H

#include "hw/virtio/virtio.h"
#include "qom/object.h"

#define TYPE_VIRTIO_RTC "virtio-rtc-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIORtc, VIRTIO_RTC)

struct VirtIORtc {
    VirtIODevice parent_obj;
    VirtQueue *vq;
};

#endif
