/*
 * QEMU Macintosh Nubus Virtio MMIO card
 *
 * Copyright (c) 2023 Mark Cave-Ayland <mark.cave-ayland@ilande.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NUBUS_VIRTIO_MMIO_H
#define HW_NUBUS_VIRTIO_MMIO_H

#include "hw/nubus/nubus.h"
#include "qom/object.h"
#include "hw/intc/goldfish_pic.h"
#include "hw/virtio/virtio-mmio.h"

#define TYPE_NUBUS_VIRTIO_MMIO "nubus-virtio-mmio"
OBJECT_DECLARE_TYPE(NubusVirtioMMIO, NubusVirtioMMIODeviceClass,
                    NUBUS_VIRTIO_MMIO)

struct NubusVirtioMMIODeviceClass {
    DeviceClass parent_class;

    DeviceRealize parent_realize;
};

#define NUBUS_VIRTIO_MMIO_NUM_DEVICES 32

struct NubusVirtioMMIO {
    NubusDevice parent_obj;

    GoldfishPICState pic;
    VirtIOMMIOProxy virtio_mmio[NUBUS_VIRTIO_MMIO_NUM_DEVICES];
};

#endif
