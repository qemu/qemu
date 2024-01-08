/*
 * VFIO BASE CONTAINER
 *
 * Copyright (C) 2023 Intel Corporation.
 * Copyright Red Hat, Inc. 2023
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CONTAINER_BASE_H
#define HW_VFIO_VFIO_CONTAINER_BASE_H

#include "exec/memory.h"

typedef struct VFIODevice VFIODevice;
typedef struct VFIOIOMMUClass VFIOIOMMUClass;

typedef struct {
    unsigned long *bitmap;
    hwaddr size;
    hwaddr pages;
} VFIOBitmap;

typedef struct VFIOAddressSpace {
    AddressSpace *as;
    QLIST_HEAD(, VFIOContainerBase) containers;
    QLIST_ENTRY(VFIOAddressSpace) list;
} VFIOAddressSpace;

/*
 * This is the base object for vfio container backends
 */
typedef struct VFIOContainerBase {
    const VFIOIOMMUClass *ops;
    VFIOAddressSpace *space;
    MemoryListener listener;
    Error *error;
    bool initialized;
    uint64_t dirty_pgsizes;
    uint64_t max_dirty_bitmap_size;
    unsigned long pgsizes;
    unsigned int dma_max_mappings;
    bool dirty_pages_supported;
    QLIST_HEAD(, VFIOGuestIOMMU) giommu_list;
    QLIST_HEAD(, VFIORamDiscardListener) vrdl_list;
    QLIST_ENTRY(VFIOContainerBase) next;
    QLIST_HEAD(, VFIODevice) device_list;
    GList *iova_ranges;
} VFIOContainerBase;

typedef struct VFIOGuestIOMMU {
    VFIOContainerBase *bcontainer;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(VFIOGuestIOMMU) giommu_next;
} VFIOGuestIOMMU;

typedef struct VFIORamDiscardListener {
    VFIOContainerBase *bcontainer;
    MemoryRegion *mr;
    hwaddr offset_within_address_space;
    hwaddr size;
    uint64_t granularity;
    RamDiscardListener listener;
    QLIST_ENTRY(VFIORamDiscardListener) next;
} VFIORamDiscardListener;

int vfio_container_dma_map(VFIOContainerBase *bcontainer,
                           hwaddr iova, ram_addr_t size,
                           void *vaddr, bool readonly);
int vfio_container_dma_unmap(VFIOContainerBase *bcontainer,
                             hwaddr iova, ram_addr_t size,
                             IOMMUTLBEntry *iotlb);
int vfio_container_add_section_window(VFIOContainerBase *bcontainer,
                                      MemoryRegionSection *section,
                                      Error **errp);
void vfio_container_del_section_window(VFIOContainerBase *bcontainer,
                                       MemoryRegionSection *section);
int vfio_container_set_dirty_page_tracking(VFIOContainerBase *bcontainer,
                                           bool start);
int vfio_container_query_dirty_bitmap(const VFIOContainerBase *bcontainer,
                                      VFIOBitmap *vbmap,
                                      hwaddr iova, hwaddr size);

void vfio_container_init(VFIOContainerBase *bcontainer,
                         VFIOAddressSpace *space,
                         const VFIOIOMMUClass *ops);
void vfio_container_destroy(VFIOContainerBase *bcontainer);


#define TYPE_VFIO_IOMMU "vfio-iommu"
#define TYPE_VFIO_IOMMU_LEGACY TYPE_VFIO_IOMMU "-legacy"
#define TYPE_VFIO_IOMMU_SPAPR TYPE_VFIO_IOMMU "-spapr"
#define TYPE_VFIO_IOMMU_IOMMUFD TYPE_VFIO_IOMMU "-iommufd"

/*
 * VFIOContainerBase is not an abstract QOM object because it felt
 * unnecessary to expose all the IOMMU backends to the QEMU machine
 * and human interface. However, we can still abstract the IOMMU
 * backend handlers using a QOM interface class. This provides more
 * flexibility when referencing the various implementations.
 */
DECLARE_CLASS_CHECKERS(VFIOIOMMUClass, VFIO_IOMMU, TYPE_VFIO_IOMMU)

struct VFIOIOMMUClass {
    InterfaceClass parent_class;

    /* basic feature */
    int (*setup)(VFIOContainerBase *bcontainer, Error **errp);
    int (*dma_map)(const VFIOContainerBase *bcontainer,
                   hwaddr iova, ram_addr_t size,
                   void *vaddr, bool readonly);
    int (*dma_unmap)(const VFIOContainerBase *bcontainer,
                     hwaddr iova, ram_addr_t size,
                     IOMMUTLBEntry *iotlb);
    int (*attach_device)(const char *name, VFIODevice *vbasedev,
                         AddressSpace *as, Error **errp);
    void (*detach_device)(VFIODevice *vbasedev);
    /* migration feature */
    int (*set_dirty_page_tracking)(const VFIOContainerBase *bcontainer,
                                   bool start);
    int (*query_dirty_bitmap)(const VFIOContainerBase *bcontainer,
                              VFIOBitmap *vbmap,
                              hwaddr iova, hwaddr size);
    /* PCI specific */
    int (*pci_hot_reset)(VFIODevice *vbasedev, bool single);

    /* SPAPR specific */
    int (*add_window)(VFIOContainerBase *bcontainer,
                      MemoryRegionSection *section,
                      Error **errp);
    void (*del_window)(VFIOContainerBase *bcontainer,
                       MemoryRegionSection *section);
    void (*release)(VFIOContainerBase *bcontainer);
};
#endif /* HW_VFIO_VFIO_CONTAINER_BASE_H */
