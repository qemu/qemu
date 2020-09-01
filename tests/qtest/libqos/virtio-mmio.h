/*
 * libqos virtio MMIO definitions
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_VIRTIO_MMIO_H
#define LIBQOS_VIRTIO_MMIO_H

#include "virtio.h"
#include "qgraph.h"

#define QVIRTIO_MMIO_MAGIC_VALUE        0x000
#define QVIRTIO_MMIO_VERSION            0x004
#define QVIRTIO_MMIO_DEVICE_ID          0x008
#define QVIRTIO_MMIO_VENDOR_ID          0x00C
#define QVIRTIO_MMIO_HOST_FEATURES      0x010
#define QVIRTIO_MMIO_HOST_FEATURES_SEL  0x014
#define QVIRTIO_MMIO_GUEST_FEATURES     0x020
#define QVIRTIO_MMIO_GUEST_FEATURES_SEL 0x024
#define QVIRTIO_MMIO_GUEST_PAGE_SIZE    0x028
#define QVIRTIO_MMIO_QUEUE_SEL          0x030
#define QVIRTIO_MMIO_QUEUE_NUM_MAX      0x034
#define QVIRTIO_MMIO_QUEUE_NUM          0x038
#define QVIRTIO_MMIO_QUEUE_ALIGN        0x03C
#define QVIRTIO_MMIO_QUEUE_PFN          0x040
#define QVIRTIO_MMIO_QUEUE_NOTIFY       0x050
#define QVIRTIO_MMIO_INTERRUPT_STATUS   0x060
#define QVIRTIO_MMIO_INTERRUPT_ACK      0x064
#define QVIRTIO_MMIO_DEVICE_STATUS      0x070
#define QVIRTIO_MMIO_DEVICE_SPECIFIC    0x100

typedef struct QVirtioMMIODevice {
    QOSGraphObject obj;
    QVirtioDevice vdev;
    QTestState *qts;
    uint64_t addr;
    uint32_t page_size;
    uint32_t features; /* As it cannot be read later, save it */
    uint32_t version;
} QVirtioMMIODevice;

extern const QVirtioBus qvirtio_mmio;

void qvirtio_mmio_init_device(QVirtioMMIODevice *dev, QTestState *qts,
                              uint64_t addr, uint32_t page_size);

#endif
