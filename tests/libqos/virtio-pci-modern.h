/*
 * libqos virtio PCI VIRTIO 1.0 definitions
 *
 * Copyright (c) 2019 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_VIRTIO_PCI_MODERN_H
#define LIBQOS_VIRTIO_PCI_MODERN_H

#include "virtio-pci.h"

bool qvirtio_pci_init_virtio_1(QVirtioPCIDevice *dev);

#endif /* LIBQOS_VIRTIO_PCI_MODERN_H */
