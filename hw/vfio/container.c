/*
 * generic functions used by VFIO devices
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include "hw/vfio/vfio-common.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "hw/hw.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "sysemu/reset.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/migration.h"

VFIOGroupList vfio_group_list =
    QLIST_HEAD_INITIALIZER(vfio_group_list);

static int vfio_ram_block_discard_disable(VFIOContainer *container, bool state)
{
    switch (container->iommu_type) {
    case VFIO_TYPE1v2_IOMMU:
    case VFIO_TYPE1_IOMMU:
        /*
         * We support coordinated discarding of RAM via the RamDiscardManager.
         */
        return ram_block_uncoordinated_discard_disable(state);
    default:
        /*
         * VFIO_SPAPR_TCE_IOMMU most probably works just fine with
         * RamDiscardManager, however, it is completely untested.
         *
         * VFIO_SPAPR_TCE_v2_IOMMU with "DMA memory preregistering" does
         * completely the opposite of managing mapping/pinning dynamically as
         * required by RamDiscardManager. We would have to special-case sections
         * with a RamDiscardManager.
         */
        return ram_block_discard_disable(state);
    }
}

static int vfio_dma_unmap_bitmap(VFIOContainer *container,
                                 hwaddr iova, ram_addr_t size,
                                 IOMMUTLBEntry *iotlb)
{
    struct vfio_iommu_type1_dma_unmap *unmap;
    struct vfio_bitmap *bitmap;
    VFIOBitmap vbmap;
    int ret;

    ret = vfio_bitmap_alloc(&vbmap, size);
    if (ret) {
        return ret;
    }

    unmap = g_malloc0(sizeof(*unmap) + sizeof(*bitmap));

    unmap->argsz = sizeof(*unmap) + sizeof(*bitmap);
    unmap->iova = iova;
    unmap->size = size;
    unmap->flags |= VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP;
    bitmap = (struct vfio_bitmap *)&unmap->data;

    /*
     * cpu_physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty. Hence set bitmap_pgsize
     * to qemu_real_host_page_size.
     */
    bitmap->pgsize = qemu_real_host_page_size();
    bitmap->size = vbmap.size;
    bitmap->data = (__u64 *)vbmap.bitmap;

    if (vbmap.size > container->max_dirty_bitmap_size) {
        error_report("UNMAP: Size of bitmap too big 0x%"PRIx64, vbmap.size);
        ret = -E2BIG;
        goto unmap_exit;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, unmap);
    if (!ret) {
        cpu_physical_memory_set_dirty_lebitmap(vbmap.bitmap,
                iotlb->translated_addr, vbmap.pages);
    } else {
        error_report("VFIO_UNMAP_DMA with DIRTY_BITMAP : %m");
    }

unmap_exit:
    g_free(unmap);
    g_free(vbmap.bitmap);

    return ret;
}

/*
 * DMA - Mapping and unmapping for the "type1" IOMMU interface used on x86
 */
int vfio_dma_unmap(VFIOContainer *container, hwaddr iova,
                   ram_addr_t size, IOMMUTLBEntry *iotlb)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = iova,
        .size = size,
    };
    bool need_dirty_sync = false;
    int ret;

    if (iotlb && vfio_devices_all_running_and_mig_active(container)) {
        if (!vfio_devices_all_device_dirty_tracking(container) &&
            container->dirty_pages_supported) {
            return vfio_dma_unmap_bitmap(container, iova, size, iotlb);
        }

        need_dirty_sync = true;
    }

    while (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        /*
         * The type1 backend has an off-by-one bug in the kernel (71a7d3d78e3c
         * v4.15) where an overflow in its wrap-around check prevents us from
         * unmapping the last page of the address space.  Test for the error
         * condition and re-try the unmap excluding the last page.  The
         * expectation is that we've never mapped the last page anyway and this
         * unmap request comes via vIOMMU support which also makes it unlikely
         * that this page is used.  This bug was introduced well after type1 v2
         * support was introduced, so we shouldn't need to test for v1.  A fix
         * is queued for kernel v5.0 so this workaround can be removed once
         * affected kernels are sufficiently deprecated.
         */
        if (errno == EINVAL && unmap.size && !(unmap.iova + unmap.size) &&
            container->iommu_type == VFIO_TYPE1v2_IOMMU) {
            trace_vfio_dma_unmap_overflow_workaround();
            unmap.size -= 1ULL << ctz64(container->pgsizes);
            continue;
        }
        error_report("VFIO_UNMAP_DMA failed: %s", strerror(errno));
        return -errno;
    }

    if (need_dirty_sync) {
        ret = vfio_get_dirty_bitmap(container, iova, size,
                                    iotlb->translated_addr);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

int vfio_dma_map(VFIOContainer *container, hwaddr iova,
                 ram_addr_t size, void *vaddr, bool readonly)
{
    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .flags = VFIO_DMA_MAP_FLAG_READ,
        .vaddr = (__u64)(uintptr_t)vaddr,
        .iova = iova,
        .size = size,
    };

    if (!readonly) {
        map.flags |= VFIO_DMA_MAP_FLAG_WRITE;
    }

    /*
     * Try the mapping, if it fails with EBUSY, unmap the region and try
     * again.  This shouldn't be necessary, but we sometimes see it in
     * the VGA ROM space.
     */
    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0 ||
        (errno == EBUSY && vfio_dma_unmap(container, iova, size, NULL) == 0 &&
         ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0)) {
        return 0;
    }

    error_report("VFIO_MAP_DMA failed: %s", strerror(errno));
    return -errno;
}

int vfio_set_dirty_page_tracking(VFIOContainer *container, bool start)
{
    int ret;
    struct vfio_iommu_type1_dirty_bitmap dirty = {
        .argsz = sizeof(dirty),
    };

    if (!container->dirty_pages_supported) {
        return 0;
    }

    if (start) {
        dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_START;
    } else {
        dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_DIRTY_PAGES, &dirty);
    if (ret) {
        ret = -errno;
        error_report("Failed to set dirty tracking flag 0x%x errno: %d",
                     dirty.flags, errno);
    }

    return ret;
}

int vfio_query_dirty_bitmap(VFIOContainer *container, VFIOBitmap *vbmap,
                            hwaddr iova, hwaddr size)
{
    struct vfio_iommu_type1_dirty_bitmap *dbitmap;
    struct vfio_iommu_type1_dirty_bitmap_get *range;
    int ret;

    dbitmap = g_malloc0(sizeof(*dbitmap) + sizeof(*range));

    dbitmap->argsz = sizeof(*dbitmap) + sizeof(*range);
    dbitmap->flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP;
    range = (struct vfio_iommu_type1_dirty_bitmap_get *)&dbitmap->data;
    range->iova = iova;
    range->size = size;

    /*
     * cpu_physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty. Hence set bitmap's pgsize
     * to qemu_real_host_page_size.
     */
    range->bitmap.pgsize = qemu_real_host_page_size();
    range->bitmap.size = vbmap->size;
    range->bitmap.data = (__u64 *)vbmap->bitmap;

    ret = ioctl(container->fd, VFIO_IOMMU_DIRTY_PAGES, dbitmap);
    if (ret) {
        ret = -errno;
        error_report("Failed to get dirty bitmap for iova: 0x%"PRIx64
                " size: 0x%"PRIx64" err: %d", (uint64_t)range->iova,
                (uint64_t)range->size, errno);
    }

    g_free(dbitmap);

    return ret;
}

static struct vfio_info_cap_header *
vfio_get_iommu_type1_info_cap(struct vfio_iommu_type1_info *info, uint16_t id)
{
    if (!(info->flags & VFIO_IOMMU_INFO_CAPS)) {
        return NULL;
    }

    return vfio_get_cap((void *)info, info->cap_offset, id);
}

bool vfio_get_info_dma_avail(struct vfio_iommu_type1_info *info,
                             unsigned int *avail)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_iommu_type1_info_dma_avail *cap;

    /* If the capability cannot be found, assume no DMA limiting */
    hdr = vfio_get_iommu_type1_info_cap(info,
                                        VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL);
    if (!hdr) {
        return false;
    }

    if (avail != NULL) {
        cap = (void *) hdr;
        *avail = cap->avail;
    }

    return true;
}

static bool vfio_get_info_iova_range(struct vfio_iommu_type1_info *info,
                                     VFIOContainer *container)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_iommu_type1_info_cap_iova_range *cap;

    hdr = vfio_get_iommu_type1_info_cap(info,
                                        VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE);
    if (!hdr) {
        return false;
    }

    cap = (void *)hdr;

    for (int i = 0; i < cap->nr_iovas; i++) {
        Range *range = g_new(Range, 1);

        range_set_bounds(range, cap->iova_ranges[i].start,
                         cap->iova_ranges[i].end);
        container->iova_ranges =
            range_list_insert(container->iova_ranges, range);
    }

    return true;
}

static void vfio_kvm_device_add_group(VFIOGroup *group)
{
    Error *err = NULL;

    if (vfio_kvm_device_add_fd(group->fd, &err)) {
        error_reportf_err(err, "group ID %d: ", group->groupid);
    }
}

static void vfio_kvm_device_del_group(VFIOGroup *group)
{
    Error *err = NULL;

    if (vfio_kvm_device_del_fd(group->fd, &err)) {
        error_reportf_err(err, "group ID %d: ", group->groupid);
    }
}

/*
 * vfio_get_iommu_type - selects the richest iommu_type (v2 first)
 */
static int vfio_get_iommu_type(VFIOContainer *container,
                               Error **errp)
{
    int iommu_types[] = { VFIO_TYPE1v2_IOMMU, VFIO_TYPE1_IOMMU,
                          VFIO_SPAPR_TCE_v2_IOMMU, VFIO_SPAPR_TCE_IOMMU };
    int i;

    for (i = 0; i < ARRAY_SIZE(iommu_types); i++) {
        if (ioctl(container->fd, VFIO_CHECK_EXTENSION, iommu_types[i])) {
            return iommu_types[i];
        }
    }
    error_setg(errp, "No available IOMMU models");
    return -EINVAL;
}

static int vfio_init_container(VFIOContainer *container, int group_fd,
                               Error **errp)
{
    int iommu_type, ret;

    iommu_type = vfio_get_iommu_type(container, errp);
    if (iommu_type < 0) {
        return iommu_type;
    }

    ret = ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container->fd);
    if (ret) {
        error_setg_errno(errp, errno, "Failed to set group container");
        return -errno;
    }

    while (ioctl(container->fd, VFIO_SET_IOMMU, iommu_type)) {
        if (iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
            /*
             * On sPAPR, despite the IOMMU subdriver always advertises v1 and
             * v2, the running platform may not support v2 and there is no
             * way to guess it until an IOMMU group gets added to the container.
             * So in case it fails with v2, try v1 as a fallback.
             */
            iommu_type = VFIO_SPAPR_TCE_IOMMU;
            continue;
        }
        error_setg_errno(errp, errno, "Failed to set iommu for container");
        return -errno;
    }

    container->iommu_type = iommu_type;
    return 0;
}

static int vfio_get_iommu_info(VFIOContainer *container,
                               struct vfio_iommu_type1_info **info)
{

    size_t argsz = sizeof(struct vfio_iommu_type1_info);

    *info = g_new0(struct vfio_iommu_type1_info, 1);
again:
    (*info)->argsz = argsz;

    if (ioctl(container->fd, VFIO_IOMMU_GET_INFO, *info)) {
        g_free(*info);
        *info = NULL;
        return -errno;
    }

    if (((*info)->argsz > argsz)) {
        argsz = (*info)->argsz;
        *info = g_realloc(*info, argsz);
        goto again;
    }

    return 0;
}

static struct vfio_info_cap_header *
vfio_get_iommu_info_cap(struct vfio_iommu_type1_info *info, uint16_t id)
{
    struct vfio_info_cap_header *hdr;
    void *ptr = info;

    if (!(info->flags & VFIO_IOMMU_INFO_CAPS)) {
        return NULL;
    }

    for (hdr = ptr + info->cap_offset; hdr != ptr; hdr = ptr + hdr->next) {
        if (hdr->id == id) {
            return hdr;
        }
    }

    return NULL;
}

static void vfio_get_iommu_info_migration(VFIOContainer *container,
                                          struct vfio_iommu_type1_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_iommu_type1_info_cap_migration *cap_mig;

    hdr = vfio_get_iommu_info_cap(info, VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION);
    if (!hdr) {
        return;
    }

    cap_mig = container_of(hdr, struct vfio_iommu_type1_info_cap_migration,
                            header);

    /*
     * cpu_physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty.
     */
    if (cap_mig->pgsize_bitmap & qemu_real_host_page_size()) {
        container->dirty_pages_supported = true;
        container->max_dirty_bitmap_size = cap_mig->max_dirty_bitmap_size;
        container->dirty_pgsizes = cap_mig->pgsize_bitmap;
    }
}

static void vfio_free_container(VFIOContainer *container)
{
    g_list_free_full(container->iova_ranges, g_free);
    g_free(container);
}

static int vfio_connect_container(VFIOGroup *group, AddressSpace *as,
                                  Error **errp)
{
    VFIOContainer *container;
    int ret, fd;
    VFIOAddressSpace *space;

    space = vfio_get_address_space(as);

    /*
     * VFIO is currently incompatible with discarding of RAM insofar as the
     * madvise to purge (zap) the page from QEMU's address space does not
     * interact with the memory API and therefore leaves stale virtual to
     * physical mappings in the IOMMU if the page was previously pinned.  We
     * therefore set discarding broken for each group added to a container,
     * whether the container is used individually or shared.  This provides
     * us with options to allow devices within a group to opt-in and allow
     * discarding, so long as it is done consistently for a group (for instance
     * if the device is an mdev device where it is known that the host vendor
     * driver will never pin pages outside of the working set of the guest
     * driver, which would thus not be discarding candidates).
     *
     * The first opportunity to induce pinning occurs here where we attempt to
     * attach the group to existing containers within the AddressSpace.  If any
     * pages are already zapped from the virtual address space, such as from
     * previous discards, new pinning will cause valid mappings to be
     * re-established.  Likewise, when the overall MemoryListener for a new
     * container is registered, a replay of mappings within the AddressSpace
     * will occur, re-establishing any previously zapped pages as well.
     *
     * Especially virtio-balloon is currently only prevented from discarding
     * new memory, it will not yet set ram_block_discard_set_required() and
     * therefore, neither stops us here or deals with the sudden memory
     * consumption of inflated memory.
     *
     * We do support discarding of memory coordinated via the RamDiscardManager
     * with some IOMMU types. vfio_ram_block_discard_disable() handles the
     * details once we know which type of IOMMU we are using.
     */

    QLIST_FOREACH(container, &space->containers, next) {
        if (!ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &container->fd)) {
            ret = vfio_ram_block_discard_disable(container, true);
            if (ret) {
                error_setg_errno(errp, -ret,
                                 "Cannot set discarding of RAM broken");
                if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER,
                          &container->fd)) {
                    error_report("vfio: error disconnecting group %d from"
                                 " container", group->groupid);
                }
                return ret;
            }
            group->container = container;
            QLIST_INSERT_HEAD(&container->group_list, group, container_next);
            vfio_kvm_device_add_group(group);
            return 0;
        }
    }

    fd = qemu_open_old("/dev/vfio/vfio", O_RDWR);
    if (fd < 0) {
        error_setg_errno(errp, errno, "failed to open /dev/vfio/vfio");
        ret = -errno;
        goto put_space_exit;
    }

    ret = ioctl(fd, VFIO_GET_API_VERSION);
    if (ret != VFIO_API_VERSION) {
        error_setg(errp, "supported vfio version: %d, "
                   "reported version: %d", VFIO_API_VERSION, ret);
        ret = -EINVAL;
        goto close_fd_exit;
    }

    container = g_malloc0(sizeof(*container));
    container->space = space;
    container->fd = fd;
    container->error = NULL;
    container->dirty_pages_supported = false;
    container->dma_max_mappings = 0;
    container->iova_ranges = NULL;
    QLIST_INIT(&container->giommu_list);
    QLIST_INIT(&container->vrdl_list);

    ret = vfio_init_container(container, group->fd, errp);
    if (ret) {
        goto free_container_exit;
    }

    ret = vfio_ram_block_discard_disable(container, true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto free_container_exit;
    }

    switch (container->iommu_type) {
    case VFIO_TYPE1v2_IOMMU:
    case VFIO_TYPE1_IOMMU:
    {
        struct vfio_iommu_type1_info *info;

        ret = vfio_get_iommu_info(container, &info);
        if (ret) {
            error_setg_errno(errp, -ret, "Failed to get VFIO IOMMU info");
            goto enable_discards_exit;
        }

        if (info->flags & VFIO_IOMMU_INFO_PGSIZES) {
            container->pgsizes = info->iova_pgsizes;
        } else {
            container->pgsizes = qemu_real_host_page_size();
        }

        if (!vfio_get_info_dma_avail(info, &container->dma_max_mappings)) {
            container->dma_max_mappings = 65535;
        }

        vfio_get_info_iova_range(info, container);

        vfio_get_iommu_info_migration(container, info);
        g_free(info);
        break;
    }
    case VFIO_SPAPR_TCE_v2_IOMMU:
    case VFIO_SPAPR_TCE_IOMMU:
    {
        ret = vfio_spapr_container_init(container, errp);
        if (ret) {
            goto enable_discards_exit;
        }
        break;
    }
    }

    vfio_kvm_device_add_group(group);

    QLIST_INIT(&container->group_list);
    QLIST_INSERT_HEAD(&space->containers, container, next);

    group->container = container;
    QLIST_INSERT_HEAD(&container->group_list, group, container_next);

    container->listener = vfio_memory_listener;

    memory_listener_register(&container->listener, container->space->as);

    if (container->error) {
        ret = -1;
        error_propagate_prepend(errp, container->error,
            "memory listener initialization failed: ");
        goto listener_release_exit;
    }

    container->initialized = true;

    return 0;
listener_release_exit:
    QLIST_REMOVE(group, container_next);
    QLIST_REMOVE(container, next);
    vfio_kvm_device_del_group(group);
    memory_listener_unregister(&container->listener);
    if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU ||
        container->iommu_type == VFIO_SPAPR_TCE_IOMMU) {
        vfio_spapr_container_deinit(container);
    }

enable_discards_exit:
    vfio_ram_block_discard_disable(container, false);

free_container_exit:
    vfio_free_container(container);

close_fd_exit:
    close(fd);

put_space_exit:
    vfio_put_address_space(space);

    return ret;
}

static void vfio_disconnect_container(VFIOGroup *group)
{
    VFIOContainer *container = group->container;

    QLIST_REMOVE(group, container_next);
    group->container = NULL;

    /*
     * Explicitly release the listener first before unset container,
     * since unset may destroy the backend container if it's the last
     * group.
     */
    if (QLIST_EMPTY(&container->group_list)) {
        memory_listener_unregister(&container->listener);
        if (container->iommu_type == VFIO_SPAPR_TCE_v2_IOMMU ||
            container->iommu_type == VFIO_SPAPR_TCE_IOMMU) {
            vfio_spapr_container_deinit(container);
        }
    }

    if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd)) {
        error_report("vfio: error disconnecting group %d from container",
                     group->groupid);
    }

    if (QLIST_EMPTY(&container->group_list)) {
        VFIOAddressSpace *space = container->space;
        VFIOGuestIOMMU *giommu, *tmp;

        QLIST_REMOVE(container, next);

        QLIST_FOREACH_SAFE(giommu, &container->giommu_list, giommu_next, tmp) {
            memory_region_unregister_iommu_notifier(
                    MEMORY_REGION(giommu->iommu_mr), &giommu->n);
            QLIST_REMOVE(giommu, giommu_next);
            g_free(giommu);
        }

        trace_vfio_disconnect_container(container->fd);
        close(container->fd);
        vfio_free_container(container);

        vfio_put_address_space(space);
    }
}

static VFIOGroup *vfio_get_group(int groupid, AddressSpace *as, Error **errp)
{
    VFIOGroup *group;
    char path[32];
    struct vfio_group_status status = { .argsz = sizeof(status) };

    QLIST_FOREACH(group, &vfio_group_list, next) {
        if (group->groupid == groupid) {
            /* Found it.  Now is it already in the right context? */
            if (group->container->space->as == as) {
                return group;
            } else {
                error_setg(errp, "group %d used in multiple address spaces",
                           group->groupid);
                return NULL;
            }
        }
    }

    group = g_malloc0(sizeof(*group));

    snprintf(path, sizeof(path), "/dev/vfio/%d", groupid);
    group->fd = qemu_open_old(path, O_RDWR);
    if (group->fd < 0) {
        error_setg_errno(errp, errno, "failed to open %s", path);
        goto free_group_exit;
    }

    if (ioctl(group->fd, VFIO_GROUP_GET_STATUS, &status)) {
        error_setg_errno(errp, errno, "failed to get group %d status", groupid);
        goto close_fd_exit;
    }

    if (!(status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        error_setg(errp, "group %d is not viable", groupid);
        error_append_hint(errp,
                          "Please ensure all devices within the iommu_group "
                          "are bound to their vfio bus driver.\n");
        goto close_fd_exit;
    }

    group->groupid = groupid;
    QLIST_INIT(&group->device_list);

    if (vfio_connect_container(group, as, errp)) {
        error_prepend(errp, "failed to setup container for group %d: ",
                      groupid);
        goto close_fd_exit;
    }

    QLIST_INSERT_HEAD(&vfio_group_list, group, next);

    return group;

close_fd_exit:
    close(group->fd);

free_group_exit:
    g_free(group);

    return NULL;
}

static void vfio_put_group(VFIOGroup *group)
{
    if (!group || !QLIST_EMPTY(&group->device_list)) {
        return;
    }

    if (!group->ram_block_discard_allowed) {
        vfio_ram_block_discard_disable(group->container, false);
    }
    vfio_kvm_device_del_group(group);
    vfio_disconnect_container(group);
    QLIST_REMOVE(group, next);
    trace_vfio_put_group(group->fd);
    close(group->fd);
    g_free(group);
}

static int vfio_get_device(VFIOGroup *group, const char *name,
                           VFIODevice *vbasedev, Error **errp)
{
    g_autofree struct vfio_device_info *info = NULL;
    int fd;

    fd = ioctl(group->fd, VFIO_GROUP_GET_DEVICE_FD, name);
    if (fd < 0) {
        error_setg_errno(errp, errno, "error getting device from group %d",
                         group->groupid);
        error_append_hint(errp,
                      "Verify all devices in group %d are bound to vfio-<bus> "
                      "or pci-stub and not already in use\n", group->groupid);
        return fd;
    }

    info = vfio_get_device_info(fd);
    if (!info) {
        error_setg_errno(errp, errno, "error getting device info");
        close(fd);
        return -1;
    }

    /*
     * Set discarding of RAM as not broken for this group if the driver knows
     * the device operates compatibly with discarding.  Setting must be
     * consistent per group, but since compatibility is really only possible
     * with mdev currently, we expect singleton groups.
     */
    if (vbasedev->ram_block_discard_allowed !=
        group->ram_block_discard_allowed) {
        if (!QLIST_EMPTY(&group->device_list)) {
            error_setg(errp, "Inconsistent setting of support for discarding "
                       "RAM (e.g., balloon) within group");
            close(fd);
            return -1;
        }

        if (!group->ram_block_discard_allowed) {
            group->ram_block_discard_allowed = true;
            vfio_ram_block_discard_disable(group->container, false);
        }
    }

    vbasedev->fd = fd;
    vbasedev->group = group;
    QLIST_INSERT_HEAD(&group->device_list, vbasedev, next);

    vbasedev->num_irqs = info->num_irqs;
    vbasedev->num_regions = info->num_regions;
    vbasedev->flags = info->flags;

    trace_vfio_get_device(name, info->flags, info->num_regions, info->num_irqs);

    vbasedev->reset_works = !!(info->flags & VFIO_DEVICE_FLAGS_RESET);

    return 0;
}

static void vfio_put_base_device(VFIODevice *vbasedev)
{
    if (!vbasedev->group) {
        return;
    }
    QLIST_REMOVE(vbasedev, next);
    vbasedev->group = NULL;
    trace_vfio_put_base_device(vbasedev->fd);
    close(vbasedev->fd);
}

static int vfio_device_groupid(VFIODevice *vbasedev, Error **errp)
{
    char *tmp, group_path[PATH_MAX], *group_name;
    int ret, groupid;
    ssize_t len;

    tmp = g_strdup_printf("%s/iommu_group", vbasedev->sysfsdev);
    len = readlink(tmp, group_path, sizeof(group_path));
    g_free(tmp);

    if (len <= 0 || len >= sizeof(group_path)) {
        ret = len < 0 ? -errno : -ENAMETOOLONG;
        error_setg_errno(errp, -ret, "no iommu_group found");
        return ret;
    }

    group_path[len] = 0;

    group_name = basename(group_path);
    if (sscanf(group_name, "%d", &groupid) != 1) {
        error_setg_errno(errp, errno, "failed to read %s", group_path);
        return -errno;
    }
    return groupid;
}

/*
 * vfio_attach_device: attach a device to a security context
 * @name and @vbasedev->name are likely to be different depending
 * on the type of the device, hence the need for passing @name
 */
int vfio_attach_device(char *name, VFIODevice *vbasedev,
                       AddressSpace *as, Error **errp)
{
    int groupid = vfio_device_groupid(vbasedev, errp);
    VFIODevice *vbasedev_iter;
    VFIOGroup *group;
    VFIOContainer *container;
    int ret;

    if (groupid < 0) {
        return groupid;
    }

    trace_vfio_attach_device(vbasedev->name, groupid);

    group = vfio_get_group(groupid, as, errp);
    if (!group) {
        return -ENOENT;
    }

    QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
        if (strcmp(vbasedev_iter->name, vbasedev->name) == 0) {
            error_setg(errp, "device is already attached");
            vfio_put_group(group);
            return -EBUSY;
        }
    }
    ret = vfio_get_device(group, name, vbasedev, errp);
    if (ret) {
        vfio_put_group(group);
        return ret;
    }

    container = group->container;
    vbasedev->container = container;
    QLIST_INSERT_HEAD(&container->device_list, vbasedev, container_next);
    QLIST_INSERT_HEAD(&vfio_device_list, vbasedev, global_next);

    return ret;
}

void vfio_detach_device(VFIODevice *vbasedev)
{
    VFIOGroup *group = vbasedev->group;

    if (!vbasedev->container) {
        return;
    }

    QLIST_REMOVE(vbasedev, global_next);
    QLIST_REMOVE(vbasedev, container_next);
    vbasedev->container = NULL;
    trace_vfio_detach_device(vbasedev->name, group->groupid);
    vfio_put_base_device(vbasedev);
    vfio_put_group(group);
}
