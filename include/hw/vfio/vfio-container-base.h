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
typedef struct VFIOIOMMUOps VFIOIOMMUOps;

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
    const VFIOIOMMUOps *ops;
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
int vfio_container_set_dirty_page_tracking(VFIOContainerBase *bcontainer,
                                           bool start);
int vfio_container_query_dirty_bitmap(VFIOContainerBase *bcontainer,
                                      VFIOBitmap *vbmap,
                                      hwaddr iova, hwaddr size);

void vfio_container_init(VFIOContainerBase *bcontainer,
                         VFIOAddressSpace *space,
                         const VFIOIOMMUOps *ops);
void vfio_container_destroy(VFIOContainerBase *bcontainer);

struct VFIOIOMMUOps {
    /* basic feature */
    int (*dma_map)(VFIOContainerBase *bcontainer,
                   hwaddr iova, ram_addr_t size,
                   void *vaddr, bool readonly);
    int (*dma_unmap)(VFIOContainerBase *bcontainer,
                     hwaddr iova, ram_addr_t size,
                     IOMMUTLBEntry *iotlb);
    int (*attach_device)(const char *name, VFIODevice *vbasedev,
                         AddressSpace *as, Error **errp);
    void (*detach_device)(VFIODevice *vbasedev);
    /* migration feature */
    int (*set_dirty_page_tracking)(VFIOContainerBase *bcontainer, bool start);
    int (*query_dirty_bitmap)(VFIOContainerBase *bcontainer, VFIOBitmap *vbmap,
                              hwaddr iova, hwaddr size);
    /* SPAPR specific */
    int (*add_window)(VFIOContainerBase *bcontainer,
                      MemoryRegionSection *section,
                      Error **errp);
    void (*del_window)(VFIOContainerBase *bcontainer,
                       MemoryRegionSection *section);
};
#endif /* HW_VFIO_VFIO_CONTAINER_BASE_H */
