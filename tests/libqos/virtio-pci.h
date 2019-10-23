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
#include "libqos/qgraph.h"

typedef struct QVirtioPCIMSIXOps QVirtioPCIMSIXOps;

typedef struct QVirtioPCIDevice {
    QOSGraphObject obj;
    QVirtioDevice vdev;
    QPCIDevice *pdev;
    QPCIBar bar;
    const QVirtioPCIMSIXOps *msix_ops;
    uint16_t config_msix_entry;
    uint64_t config_msix_addr;
    uint32_t config_msix_data;

    int bar_idx;

    /* VIRTIO 1.0 */
    uint32_t common_cfg_offset;
    uint32_t notify_cfg_offset;
    uint32_t notify_off_multiplier;
    uint32_t isr_cfg_offset;
    uint32_t device_cfg_offset;
} QVirtioPCIDevice;

struct QVirtioPCIMSIXOps {
    /* Set the Configuration Vector for MSI-X */
    void (*set_config_vector)(QVirtioPCIDevice *d, uint16_t entry);

    /* Set the Queue Vector for MSI-X */
    void (*set_queue_vector)(QVirtioPCIDevice *d, uint16_t vq_idx,
                             uint16_t entry);
};

typedef struct QVirtQueuePCI {
    QVirtQueue vq;
    uint16_t msix_entry;
    uint64_t msix_addr;
    uint32_t msix_data;

    /* VIRTIO 1.0 */
    uint64_t notify_offset;
} QVirtQueuePCI;

void virtio_pci_init(QVirtioPCIDevice *dev, QPCIBus *bus, QPCIAddress * addr);
QVirtioPCIDevice *virtio_pci_new(QPCIBus *bus, QPCIAddress * addr);

/* virtio-pci object functions available for subclasses that
 * override the original start_hw and destroy
 * function. All virtio-xxx-pci subclass that override must
 * take care of calling these two functions in the respective
 * places
 */
void qvirtio_pci_destructor(QOSGraphObject *obj);
void qvirtio_pci_start_hw(QOSGraphObject *obj);


void qvirtio_pci_device_enable(QVirtioPCIDevice *d);
void qvirtio_pci_device_disable(QVirtioPCIDevice *d);

void qvirtio_pci_set_msix_configuration_vector(QVirtioPCIDevice *d,
                                        QGuestAllocator *alloc, uint16_t entry);
void qvirtqueue_pci_msix_setup(QVirtioPCIDevice *d, QVirtQueuePCI *vqpci,
                                        QGuestAllocator *alloc, uint16_t entry);

/* Used by Legacy and Modern virtio-pci code */
QVirtQueue *qvirtio_pci_virtqueue_setup_common(QVirtioDevice *d,
                                               QGuestAllocator *alloc,
                                               uint16_t index);
void qvirtio_pci_virtqueue_cleanup_common(QVirtQueue *vq,
                                          QGuestAllocator *alloc);

#endif
