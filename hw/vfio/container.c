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
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include "system/tcg.h"
#include "system/ram_addr.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/vfio/vfio-container.h"
#include "hw/vfio/vfio-device.h" /* vfio_device_reset_handler */
#include "system/physmem.h"
#include "system/reset.h"
#include "vfio-helpers.h"

#include "trace.h"

static QLIST_HEAD(, VFIOAddressSpace) vfio_address_spaces =
    QLIST_HEAD_INITIALIZER(vfio_address_spaces);

VFIOAddressSpace *vfio_address_space_get(AddressSpace *as)
{
    VFIOAddressSpace *space;

    QLIST_FOREACH(space, &vfio_address_spaces, list) {
        if (space->as == as) {
            return space;
        }
    }

    /* No suitable VFIOAddressSpace, create a new one */
    space = g_malloc0(sizeof(*space));
    space->as = as;
    QLIST_INIT(&space->containers);

    if (QLIST_EMPTY(&vfio_address_spaces)) {
        qemu_register_reset(vfio_device_reset_handler, NULL);
    }

    QLIST_INSERT_HEAD(&vfio_address_spaces, space, list);

    return space;
}

void vfio_address_space_put(VFIOAddressSpace *space)
{
    if (!QLIST_EMPTY(&space->containers)) {
        return;
    }

    QLIST_REMOVE(space, list);
    g_free(space);

    if (QLIST_EMPTY(&vfio_address_spaces)) {
        qemu_unregister_reset(vfio_device_reset_handler, NULL);
    }
}

void vfio_address_space_insert(VFIOAddressSpace *space,
                               VFIOContainer *bcontainer)
{
    QLIST_INSERT_HEAD(&space->containers, bcontainer, next);
    bcontainer->space = space;
}

int vfio_container_dma_map(VFIOContainer *bcontainer,
                           hwaddr iova, uint64_t size,
                           void *vaddr, bool readonly, MemoryRegion *mr)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    RAMBlock *rb = mr->ram_block;
    int mfd = rb ? qemu_ram_get_fd(rb) : -1;

    if (mfd >= 0 && vioc->dma_map_file) {
        unsigned long start = vaddr - qemu_ram_get_host_addr(rb);
        unsigned long offset = qemu_ram_get_fd_offset(rb);

        return vioc->dma_map_file(bcontainer, iova, size, mfd, start + offset,
                                  readonly);
    }
    g_assert(vioc->dma_map);
    return vioc->dma_map(bcontainer, iova, size, vaddr, readonly, mr);
}

int vfio_container_dma_unmap(VFIOContainer *bcontainer,
                             hwaddr iova, uint64_t size,
                             IOMMUTLBEntry *iotlb, bool unmap_all)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    g_assert(vioc->dma_unmap);
    return vioc->dma_unmap(bcontainer, iova, size, iotlb, unmap_all);
}

bool vfio_container_add_section_window(VFIOContainer *bcontainer,
                                       MemoryRegionSection *section,
                                       Error **errp)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    if (!vioc->add_window) {
        return true;
    }

    return vioc->add_window(bcontainer, section, errp);
}

void vfio_container_del_section_window(VFIOContainer *bcontainer,
                                       MemoryRegionSection *section)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    if (!vioc->del_window) {
        return;
    }

    return vioc->del_window(bcontainer, section);
}

int vfio_container_set_dirty_page_tracking(VFIOContainer *bcontainer,
                                           bool start, Error **errp)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    int ret;

    if (!bcontainer->dirty_pages_supported) {
        return 0;
    }

    g_assert(vioc->set_dirty_page_tracking);
    if (bcontainer->dirty_pages_started == start) {
        return 0;
    }

    ret = vioc->set_dirty_page_tracking(bcontainer, start, errp);
    if (!ret) {
        bcontainer->dirty_pages_started = start;
    }

    return ret;
}

static bool vfio_container_devices_dirty_tracking_is_started(
    const VFIOContainer *bcontainer)
{
    VFIODevice *vbasedev;

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        if (!vbasedev->dirty_tracking) {
            return false;
        }
    }

    return true;
}

bool vfio_container_dirty_tracking_is_started(
    const VFIOContainer *bcontainer)
{
    return vfio_container_devices_dirty_tracking_is_started(bcontainer) ||
           bcontainer->dirty_pages_started;
}

bool vfio_container_devices_dirty_tracking_is_supported(
    const VFIOContainer *bcontainer)
{
    VFIODevice *vbasedev;

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        if (vbasedev->device_dirty_page_tracking == ON_OFF_AUTO_OFF) {
            return false;
        }
        if (!vbasedev->dirty_pages_supported) {
            return false;
        }
    }

    return true;
}

static int vfio_device_dma_logging_report(VFIODevice *vbasedev, hwaddr iova,
                                          hwaddr size, void *bitmap)
{
    uint64_t buf[DIV_ROUND_UP(sizeof(struct vfio_device_feature) +
                        sizeof(struct vfio_device_feature_dma_logging_report),
                        sizeof(uint64_t))] = {};
    struct vfio_device_feature *feature = (struct vfio_device_feature *)buf;
    struct vfio_device_feature_dma_logging_report *report =
        (struct vfio_device_feature_dma_logging_report *)feature->data;

    report->iova = iova;
    report->length = size;
    report->page_size = qemu_real_host_page_size();
    report->bitmap = (uintptr_t)bitmap;

    feature->argsz = sizeof(buf);
    feature->flags = VFIO_DEVICE_FEATURE_GET |
                     VFIO_DEVICE_FEATURE_DMA_LOGGING_REPORT;

    return vbasedev->io_ops->device_feature(vbasedev, feature);
}

static int vfio_container_iommu_query_dirty_bitmap(
    const VFIOContainer *bcontainer, VFIOBitmap *vbmap, hwaddr iova,
    hwaddr size, Error **errp)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    g_assert(vioc->query_dirty_bitmap);
    return vioc->query_dirty_bitmap(bcontainer, vbmap, iova, size,
                                               errp);
}

static int vfio_container_devices_query_dirty_bitmap(
    const VFIOContainer *bcontainer, VFIOBitmap *vbmap, hwaddr iova,
    hwaddr size, Error **errp)
{
    VFIODevice *vbasedev;
    int ret;

    QLIST_FOREACH(vbasedev, &bcontainer->device_list, container_next) {
        ret = vfio_device_dma_logging_report(vbasedev, iova, size,
                                             vbmap->bitmap);
        if (ret) {
            error_setg_errno(errp, -ret,
                             "%s: Failed to get DMA logging report, iova: "
                             "0x%" HWADDR_PRIx ", size: 0x%" HWADDR_PRIx,
                             vbasedev->name, iova, size);

            return ret;
        }
    }

    return 0;
}

int vfio_container_query_dirty_bitmap(const VFIOContainer *bcontainer,
                                      uint64_t iova, uint64_t size,
                                      hwaddr translated_addr, Error **errp)
{
    bool all_device_dirty_tracking =
        vfio_container_devices_dirty_tracking_is_supported(bcontainer);
    uint64_t dirty_pages;
    VFIOBitmap vbmap;
    int ret;

    if (!bcontainer->dirty_pages_supported && !all_device_dirty_tracking) {
        physical_memory_set_dirty_range(translated_addr, size,
                                            tcg_enabled() ? DIRTY_CLIENTS_ALL :
                                            DIRTY_CLIENTS_NOCODE);
        return 0;
    }

    ret = vfio_bitmap_alloc(&vbmap, size);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "Failed to allocate dirty tracking bitmap");
        return ret;
    }

    if (all_device_dirty_tracking) {
        ret = vfio_container_devices_query_dirty_bitmap(bcontainer, &vbmap, iova, size,
                                                        errp);
    } else {
        ret = vfio_container_iommu_query_dirty_bitmap(bcontainer, &vbmap, iova, size,
                                                     errp);
    }

    if (ret) {
        goto out;
    }

    dirty_pages = physical_memory_set_dirty_lebitmap(vbmap.bitmap,
                                                         translated_addr,
                                                         vbmap.pages);

    trace_vfio_container_query_dirty_bitmap(iova, size, vbmap.size,
                                            translated_addr, dirty_pages);
out:
    g_free(vbmap.bitmap);

    return ret;
}

static gpointer copy_iova_range(gconstpointer src, gpointer data)
{
     Range *source = (Range *)src;
     Range *dest = g_new(Range, 1);

     range_set_bounds(dest, range_lob(source), range_upb(source));
     return dest;
}

GList *vfio_container_get_iova_ranges(const VFIOContainer *bcontainer)
{
    assert(bcontainer);
    return g_list_copy_deep(bcontainer->iova_ranges, copy_iova_range, NULL);
}

static void vfio_container_instance_finalize(Object *obj)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(obj);
    VFIOGuestIOMMU *giommu, *tmp;

    QLIST_SAFE_REMOVE(bcontainer, next);

    QLIST_FOREACH_SAFE(giommu, &bcontainer->giommu_list, giommu_next, tmp) {
        memory_region_unregister_iommu_notifier(
                MEMORY_REGION(giommu->iommu_mr), &giommu->n);
        QLIST_REMOVE(giommu, giommu_next);
        g_free(giommu);
    }

    g_list_free_full(bcontainer->iova_ranges, g_free);
}

static void vfio_container_instance_init(Object *obj)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(obj);

    bcontainer->error = NULL;
    bcontainer->dirty_pages_supported = false;
    bcontainer->dma_max_mappings = 0;
    bcontainer->iova_ranges = NULL;
    QLIST_INIT(&bcontainer->giommu_list);
    QLIST_INIT(&bcontainer->vrdl_list);
}

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU,
        .parent = TYPE_OBJECT,
        .instance_init = vfio_container_instance_init,
        .instance_finalize = vfio_container_instance_finalize,
        .instance_size = sizeof(VFIOContainer),
        .class_size = sizeof(VFIOIOMMUClass),
        .abstract = true,
    },
};

DEFINE_TYPES(types)
