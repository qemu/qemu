/*
 * Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_VIRTIO_MMIO_H
#define HW_VIRTIO_MMIO_H

#include "hw/virtio/virtio-bus.h"
#include "qom/object.h"

/* QOM macros */
/* virtio-mmio-bus */
#define TYPE_VIRTIO_MMIO_BUS "virtio-mmio-bus"
/* This is reusing the VirtioBusState typedef from TYPE_VIRTIO_BUS */
DECLARE_OBJ_CHECKERS(VirtioBusState, VirtioBusClass,
                     VIRTIO_MMIO_BUS, TYPE_VIRTIO_MMIO_BUS)

/* virtio-mmio */
#define TYPE_VIRTIO_MMIO "virtio-mmio"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOMMIOProxy, VIRTIO_MMIO)

#define VIRT_MAGIC 0x74726976 /* 'virt' */
#define VIRT_VERSION 2
#define VIRT_VERSION_LEGACY 1
#define VIRT_VENDOR 0x554D4551 /* 'QEMU' */

typedef struct VirtIOMMIOQueue {
    uint16_t num;
    bool enabled;
    uint32_t desc[2];
    uint32_t avail[2];
    uint32_t used[2];
} VirtIOMMIOQueue;

struct VirtIOMMIOProxy {
    /* Generic */
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    bool legacy;
    /* Guest accessible state needing migration and reset */
    uint32_t host_features_sel;
    uint32_t guest_features_sel;
    uint32_t guest_page_shift;
    /* virtio-bus */
    VirtioBusState bus;
    bool format_transport_address;
    /* Fields only used for non-legacy (v2) devices */
    uint32_t guest_features[2];
    VirtIOMMIOQueue vqs[VIRTIO_QUEUE_MAX];
};

#endif
