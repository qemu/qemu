/*
 * libqos driver virtio-iommu-pci framework
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * Authors:
 *  Eric Auger <eric.auger@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#ifndef TESTS_LIBQOS_VIRTIO_IOMMU_H
#define TESTS_LIBQOS_VIRTIO_IOMMU_H

#include "qgraph.h"
#include "virtio.h"
#include "virtio-pci.h"

typedef struct QVirtioIOMMU QVirtioIOMMU;
typedef struct QVirtioIOMMUPCI QVirtioIOMMUPCI;
typedef struct QVirtioIOMMUDevice QVirtioIOMMUDevice;

struct QVirtioIOMMU {
    QVirtioDevice *vdev;
    QVirtQueue *vq;
};

struct QVirtioIOMMUPCI {
    QVirtioPCIDevice pci_vdev;
    QVirtioIOMMU iommu;
};

struct QVirtioIOMMUDevice {
    QOSGraphObject obj;
    QVirtioIOMMU iommu;
};

#endif
