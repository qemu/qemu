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

int vfio_container_add_section_window(VFIOContainerBase *bcontainer,
                                      MemoryRegionSection *section,
                                      Error **errp)
{
    if (!bcontainer->ops->add_window) {
        return 0;
    }

    return bcontainer->ops->add_window(bcontainer, section, errp);
}

void vfio_container_del_section_window(VFIOContainerBase *bcontainer,
                                       MemoryRegionSection *section)
{
    if (!bcontainer->ops->del_window) {
        return;
    }

    return bcontainer->ops->del_window(bcontainer, section);
}

int vfio_container_set_dirty_page_tracking(VFIOContainerBase *bcontainer,
                                           bool start)
{
    if (!bcontainer->dirty_pages_supported) {
        return 0;
    }

    g_assert(bcontainer->ops->set_dirty_page_tracking);
    return bcontainer->ops->set_dirty_page_tracking(bcontainer, start);
}

int vfio_container_query_dirty_bitmap(const VFIOContainerBase *bcontainer,
                                      VFIOBitmap *vbmap,
                                      hwaddr iova, hwaddr size)
{
    g_assert(bcontainer->ops->query_dirty_bitmap);
    return bcontainer->ops->query_dirty_bitmap(bcontainer, vbmap, iova, size);
}

void vfio_container_init(VFIOContainerBase *bcontainer, VFIOAddressSpace *space,
                         const VFIOIOMMUClass *ops)
{
    bcontainer->ops = ops;
    bcontainer->space = space;
    bcontainer->error = NULL;
    bcontainer->dirty_pages_supported = false;
    bcontainer->dma_max_mappings = 0;
    bcontainer->iova_ranges = NULL;
    QLIST_INIT(&bcontainer->giommu_list);
    QLIST_INIT(&bcontainer->vrdl_list);
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

    g_list_free_full(bcontainer->iova_ranges, g_free);
}

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU,
        .parent = TYPE_INTERFACE,
        .class_size = sizeof(VFIOIOMMUClass),
    },
};

DEFINE_TYPES(types)
