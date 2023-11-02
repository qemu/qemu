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
