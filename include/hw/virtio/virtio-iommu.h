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
#include "qapi/qapi-types-virtio.h"
#include "system/host_iommu_device.h"

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
    MemoryRegion root;          /* The root container of the device */
    MemoryRegion bypass_mr;     /* The alias of shared memory MR */
    GList *resv_regions;
    GList *host_resv_ranges;
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
    GHashTable *host_iommu_devices;
    IOMMUPciBus *iommu_pcibus_by_bus_num[PCI_BUS_MAX];
    PCIBus *primary_bus;
    ReservedRegion *prop_resv_regions;
    uint32_t nr_prop_resv_regions;
    GTree *domains;
    QemuRecMutex mutex;
    GTree *endpoints;
    bool boot_bypass;
    Notifier machine_done;
    bool granule_frozen;
    GranuleMode granule_mode;
    uint8_t aw_bits;
};

#endif
