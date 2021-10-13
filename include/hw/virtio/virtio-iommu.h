/*
 * virtio-iommu device
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_VIRTIO_IOMMU_H
#define QEMU_VIRTIO_IOMMU_H

#include "standard-headers/linux/virtio_iommu.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"
#include "qom/object.h"

#define TYPE_VIRTIO_IOMMU "virtio-iommu-device"
#define TYPE_VIRTIO_IOMMU_PCI "virtio-iommu-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOIOMMU, VIRTIO_IOMMU)

#define TYPE_VIRTIO_IOMMU_MEMORY_REGION "virtio-iommu-memory-region"

typedef struct IOMMUDevice {
    void         *viommu;
    PCIBus       *bus;
    int           devfn;
    IOMMUMemoryRegion  iommu_mr;
    AddressSpace  as;
} IOMMUDevice;

typedef struct IOMMUPciBus {
    PCIBus       *bus;
    IOMMUDevice  *pbdev[]; /* Parent array is sparse, so dynamically alloc */
} IOMMUPciBus;

struct VirtIOIOMMU {
    VirtIODevice parent_obj;
    VirtQueue *req_vq;
    VirtQueue *event_vq;
    struct virtio_iommu_config config;
    uint64_t features;
    GHashTable *as_by_busptr;
    IOMMUPciBus *iommu_pcibus_by_bus_num[PCI_BUS_MAX];
    PCIBus *primary_bus;
    ReservedRegion *reserved_regions;
    uint32_t nb_reserved_regions;
    GTree *domains;
    QemuMutex mutex;
    GTree *endpoints;
};

#endif
