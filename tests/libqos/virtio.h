/*
 * libqos virtio definitions
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_VIRTIO_H
#define LIBQOS_VIRTIO_H

#define QVIRTIO_VENDOR_ID       0x1AF4

#define QVIRTIO_NET_DEVICE_ID   0x1
#define QVIRTIO_BLK_DEVICE_ID   0x2

typedef struct QVirtioDevice {
    /* Device type */
    uint16_t device_type;
} QVirtioDevice;

#endif
