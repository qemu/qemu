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

/*
 * This is the base object for vfio container backends
 */
typedef struct VFIOContainerBase {
    const VFIOIOMMUOps *ops;
} VFIOContainerBase;

int vfio_container_dma_map(VFIOContainerBase *bcontainer,
                           hwaddr iova, ram_addr_t size,
                           void *vaddr, bool readonly);
int vfio_container_dma_unmap(VFIOContainerBase *bcontainer,
                             hwaddr iova, ram_addr_t size,
                             IOMMUTLBEntry *iotlb);

void vfio_container_init(VFIOContainerBase *bcontainer,
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
};
#endif /* HW_VFIO_VFIO_CONTAINER_BASE_H */
