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

#define QVIRTIO_RESET           0x0
#define QVIRTIO_ACKNOWLEDGE     0x1
#define QVIRTIO_DRIVER          0x2

#define QVIRTIO_NET_DEVICE_ID   0x1
#define QVIRTIO_BLK_DEVICE_ID   0x2

typedef struct QVirtioDevice {
    /* Device type */
    uint16_t device_type;
} QVirtioDevice;

typedef struct QVirtioBus {
    uint8_t (*config_readb)(QVirtioDevice *d, void *addr);
    uint16_t (*config_readw)(QVirtioDevice *d, void *addr);
    uint32_t (*config_readl)(QVirtioDevice *d, void *addr);
    uint64_t (*config_readq)(QVirtioDevice *d, void *addr);

    /* Get status of the device */
    uint8_t (*get_status)(QVirtioDevice *d);

    /* Set status of the device  */
    void (*set_status)(QVirtioDevice *d, uint8_t status);
} QVirtioBus;

uint8_t qvirtio_config_readb(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint16_t qvirtio_config_readw(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint32_t qvirtio_config_readl(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);
uint64_t qvirtio_config_readq(const QVirtioBus *bus, QVirtioDevice *d,
                                                                void *addr);

void qvirtio_reset(const QVirtioBus *bus, QVirtioDevice *d);
void qvirtio_set_acknowledge(const QVirtioBus *bus, QVirtioDevice *d);
void qvirtio_set_driver(const QVirtioBus *bus, QVirtioDevice *d);

#endif
