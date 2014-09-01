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

typedef struct QVirtioPCIDevice {
    QVirtioDevice vdev;
    QPCIDevice *pdev;
} QVirtioPCIDevice;

void qvirtio_pci_foreach(QPCIBus *bus, uint16_t device_type,
                void (*func)(QVirtioDevice *d, void *data), void *data);
QVirtioPCIDevice *qvirtio_pci_device_find(QPCIBus *bus, uint16_t device_type);
#endif
