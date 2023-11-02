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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/vfio/vfio-container-base.h"

int vfio_container_dma_map(VFIOContainerBase *bcontainer,
                           hwaddr iova, ram_addr_t size,
                           void *vaddr, bool readonly)
{
    g_assert(bcontainer->ops->dma_map);
    return bcontainer->ops->dma_map(bcontainer, iova, size, vaddr, readonly);
}

int vfio_container_dma_unmap(VFIOContainerBase *bcontainer,
                             hwaddr iova, ram_addr_t size,
                             IOMMUTLBEntry *iotlb)
{
    g_assert(bcontainer->ops->dma_unmap);
    return bcontainer->ops->dma_unmap(bcontainer, iova, size, iotlb);
}

void vfio_container_init(VFIOContainerBase *bcontainer, VFIOAddressSpace *space,
                         const VFIOIOMMUOps *ops)
{
    bcontainer->ops = ops;
    bcontainer->space = space;
    QLIST_INIT(&bcontainer->giommu_list);
}

void vfio_container_destroy(VFIOContainerBase *bcontainer)
{
    VFIOGuestIOMMU *giommu, *tmp;

    QLIST_REMOVE(bcontainer, next);

    QLIST_FOREACH_SAFE(giommu, &bcontainer->giommu_list, giommu_next, tmp) {
        memory_region_unregister_iommu_notifier(
                MEMORY_REGION(giommu->iommu_mr), &giommu->n);
        QLIST_REMOVE(giommu, giommu_next);
        g_free(giommu);
    }
}
