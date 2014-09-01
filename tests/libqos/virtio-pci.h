/*
 * libqos virtio PCI definitions
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_VIRTIO_PCI_H
#define LIBQOS_VIRTIO_PCI_H

#include "libqos/virtio.h"
#include "libqos/pci.h"

#define QVIRTIO_DEVICE_FEATURES         0x00
#define QVIRTIO_GUEST_FEATURES          0x04
#define QVIRTIO_QUEUE_ADDRESS           0x08
#define QVIRTIO_QUEUE_SIZE              0x0C
#define QVIRTIO_QUEUE_SELECT            0x0E
#define QVIRTIO_QUEUE_NOTIFY            0x10
#define QVIRTIO_DEVICE_STATUS           0x12
#define QVIRTIO_ISR_STATUS              0x13
#define QVIRTIO_MSIX_CONF_VECTOR        0x14
#define QVIRTIO_MSIX_QUEUE_VECTOR       0x16
#define QVIRTIO_DEVICE_SPECIFIC_MSIX    0x18
#define QVIRTIO_DEVICE_SPECIFIC_NO_MSIX 0x14

typedef struct QVirtioPCIDevice {
    QVirtioDevice vdev;
    QPCIDevice *pdev;
    void *addr;
} QVirtioPCIDevice;

extern const QVirtioBus qvirtio_pci;

void qvirtio_pci_foreach(QPCIBus *bus, uint16_t device_type,
                void (*func)(QVirtioDevice *d, void *data), void *data);
QVirtioPCIDevice *qvirtio_pci_device_find(QPCIBus *bus, uint16_t device_type);
void qvirtio_pci_device_enable(QVirtioPCIDevice *d);
void qvirtio_pci_device_disable(QVirtioPCIDevice *d);
#endif
