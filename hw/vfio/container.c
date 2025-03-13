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
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/ram_addr.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "system/reset.h"
#include "trace.h"
#include "qapi/error.h"
#include "pci.h"

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

static int vfio_dma_unmap_bitmap(const VFIOContainer *container,
                                 hwaddr iova, ram_addr_t size,
                                 IOMMUTLBEntry *iotlb)
{
    const VFIOContainerBase *bcontainer = &container->bcontainer;
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

    if (vbmap.size > bcontainer->max_dirty_bitmap_size) {
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
static int vfio_legacy_dma_unmap(const VFIOContainerBase *bcontainer,
                                 hwaddr iova, ram_addr_t size,
                                 IOMMUTLBEntry *iotlb)
{
    const VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                                  bcontainer);
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = iova,
        .size = size,
    };
    bool need_dirty_sync = false;
    int ret;
    Error *local_err = NULL;

    if (iotlb && vfio_devices_all_dirty_tracking_started(bcontainer)) {
        if (!vfio_devices_all_device_dirty_tracking(bcontainer) &&
            bcontainer->dirty_pages_supported) {
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
            trace_vfio_legacy_dma_unmap_overflow_workaround();
            unmap.size -= 1ULL << ctz64(bcontainer->pgsizes);
            continue;
        }
        return -errno;
    }

    if (need_dirty_sync) {
        ret = vfio_get_dirty_bitmap(bcontainer, iova, size,
                                    iotlb->translated_addr, &local_err);
        if (ret) {
            error_report_err(local_err);
            return ret;
        }
    }

    return 0;
}

static int vfio_legacy_dma_map(const VFIOContainerBase *bcontainer, hwaddr iova,
                               ram_addr_t size, void *vaddr, bool readonly)
{
    const VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                                  bcontainer);
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
        (errno == EBUSY &&
         vfio_legacy_dma_unmap(bcontainer, iova, size, NULL) == 0 &&
         ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0)) {
        return 0;
    }

    return -errno;
}

static int
vfio_legacy_set_dirty_page_tracking(const VFIOContainerBase *bcontainer,
                                    bool start, Error **errp)
{
    const VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                                  bcontainer);
    int ret;
    struct vfio_iommu_type1_dirty_bitmap dirty = {
        .argsz = sizeof(dirty),
    };

    if (start) {
        dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_START;
    } else {
        dirty.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP;
    }

    ret = ioctl(container->fd, VFIO_IOMMU_DIRTY_PAGES, &dirty);
    if (ret) {
        ret = -errno;
        error_setg_errno(errp, errno, "Failed to set dirty tracking flag 0x%x",
                         dirty.flags);
    }

    return ret;
}

static int vfio_legacy_query_dirty_bitmap(const VFIOContainerBase *bcontainer,
                      VFIOBitmap *vbmap, hwaddr iova, hwaddr size, Error **errp)
{
    const VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                                  bcontainer);
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
        error_setg_errno(errp, errno,
                         "Failed to get dirty bitmap for iova: 0x%"PRIx64
                         " size: 0x%"PRIx64, (uint64_t)range->iova,
                         (uint64_t)range->size);
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
                                     VFIOContainerBase *bcontainer)
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
        bcontainer->iova_ranges =
            range_list_insert(bcontainer->iova_ranges, range);
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
static int vfio_get_iommu_type(int container_fd,
                               Error **errp)
{
    int iommu_types[] = { VFIO_TYPE1v2_IOMMU, VFIO_TYPE1_IOMMU,
                          VFIO_SPAPR_TCE_v2_IOMMU, VFIO_SPAPR_TCE_IOMMU };
    int i;

    for (i = 0; i < ARRAY_SIZE(iommu_types); i++) {
        if (ioctl(container_fd, VFIO_CHECK_EXTENSION, iommu_types[i])) {
            return iommu_types[i];
        }
    }
    error_setg(errp, "No available IOMMU models");
    return -EINVAL;
}

/*
 * vfio_get_iommu_ops - get a VFIOIOMMUClass associated with a type
 */
static const char *vfio_get_iommu_class_name(int iommu_type)
{
    switch (iommu_type) {
    case VFIO_TYPE1v2_IOMMU:
    case VFIO_TYPE1_IOMMU:
        return TYPE_VFIO_IOMMU_LEGACY;
        break;
    case VFIO_SPAPR_TCE_v2_IOMMU:
    case VFIO_SPAPR_TCE_IOMMU:
        return TYPE_VFIO_IOMMU_SPAPR;
        break;
    default:
        g_assert_not_reached();
    };
}

static bool vfio_set_iommu(int container_fd, int group_fd,
                           int *iommu_type, Error **errp)
{
    if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd)) {
        error_setg_errno(errp, errno, "Failed to set group container");
        return false;
    }

    while (ioctl(container_fd, VFIO_SET_IOMMU, *iommu_type)) {
        if (*iommu_type == VFIO_SPAPR_TCE_v2_IOMMU) {
            /*
             * On sPAPR, despite the IOMMU subdriver always advertises v1 and
             * v2, the running platform may not support v2 and there is no
             * way to guess it until an IOMMU group gets added to the container.
             * So in case it fails with v2, try v1 as a fallback.
             */
            *iommu_type = VFIO_SPAPR_TCE_IOMMU;
            continue;
        }
        error_setg_errno(errp, errno, "Failed to set iommu for container");
        return false;
    }

    return true;
}

static VFIOContainer *vfio_create_container(int fd, VFIOGroup *group,
                                            Error **errp)
{
    int iommu_type;
    const char *vioc_name;
    VFIOContainer *container;

    iommu_type = vfio_get_iommu_type(fd, errp);
    if (iommu_type < 0) {
        return NULL;
    }

    if (!vfio_set_iommu(fd, group->fd, &iommu_type, errp)) {
        return NULL;
    }

    vioc_name = vfio_get_iommu_class_name(iommu_type);

    container = VFIO_IOMMU_LEGACY(object_new(vioc_name));
    container->fd = fd;
    container->iommu_type = iommu_type;
    return container;
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
    VFIOContainerBase *bcontainer = &container->bcontainer;

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
        bcontainer->dirty_pages_supported = true;
        bcontainer->max_dirty_bitmap_size = cap_mig->max_dirty_bitmap_size;
        bcontainer->dirty_pgsizes = cap_mig->pgsize_bitmap;
    }
}

static bool vfio_legacy_setup(VFIOContainerBase *bcontainer, Error **errp)
{
    VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                            bcontainer);
    g_autofree struct vfio_iommu_type1_info *info = NULL;
    int ret;

    ret = vfio_get_iommu_info(container, &info);
    if (ret) {
        error_setg_errno(errp, -ret, "Failed to get VFIO IOMMU info");
        return false;
    }

    if (info->flags & VFIO_IOMMU_INFO_PGSIZES) {
        bcontainer->pgsizes = info->iova_pgsizes;
    } else {
        bcontainer->pgsizes = qemu_real_host_page_size();
    }

    if (!vfio_get_info_dma_avail(info, &bcontainer->dma_max_mappings)) {
        bcontainer->dma_max_mappings = 65535;
    }

    vfio_get_info_iova_range(info, bcontainer);

    vfio_get_iommu_info_migration(container, info);
    return true;
}

static bool vfio_connect_container(VFIOGroup *group, AddressSpace *as,
                                   Error **errp)
{
    VFIOContainer *container;
    VFIOContainerBase *bcontainer;
    int ret, fd;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc;

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

    QLIST_FOREACH(bcontainer, &space->containers, next) {
        container = container_of(bcontainer, VFIOContainer, bcontainer);
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
                return false;
            }
            group->container = container;
            QLIST_INSERT_HEAD(&container->group_list, group, container_next);
            vfio_kvm_device_add_group(group);
            return true;
        }
    }

    fd = qemu_open("/dev/vfio/vfio", O_RDWR, errp);
    if (fd < 0) {
        goto put_space_exit;
    }

    ret = ioctl(fd, VFIO_GET_API_VERSION);
    if (ret != VFIO_API_VERSION) {
        error_setg(errp, "supported vfio version: %d, "
                   "reported version: %d", VFIO_API_VERSION, ret);
        goto close_fd_exit;
    }

    container = vfio_create_container(fd, group, errp);
    if (!container) {
        goto close_fd_exit;
    }
    bcontainer = &container->bcontainer;

    if (!vfio_cpr_register_container(bcontainer, errp)) {
        goto free_container_exit;
    }

    ret = vfio_ram_block_discard_disable(container, true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto unregister_container_exit;
    }

    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    assert(vioc->setup);

    if (!vioc->setup(bcontainer, errp)) {
        goto enable_discards_exit;
    }

    vfio_kvm_device_add_group(group);

    vfio_address_space_insert(space, bcontainer);

    group->container = container;
    QLIST_INSERT_HEAD(&container->group_list, group, container_next);

    bcontainer->listener = vfio_memory_listener;
    memory_listener_register(&bcontainer->listener, bcontainer->space->as);

    if (bcontainer->error) {
        error_propagate_prepend(errp, bcontainer->error,
            "memory listener initialization failed: ");
        goto listener_release_exit;
    }

    bcontainer->initialized = true;

    return true;
listener_release_exit:
    QLIST_REMOVE(group, container_next);
    vfio_kvm_device_del_group(group);
    memory_listener_unregister(&bcontainer->listener);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

enable_discards_exit:
    vfio_ram_block_discard_disable(container, false);

unregister_container_exit:
    vfio_cpr_unregister_container(bcontainer);

free_container_exit:
    object_unref(container);

close_fd_exit:
    close(fd);

put_space_exit:
    vfio_put_address_space(space);

    return false;
}

static void vfio_disconnect_container(VFIOGroup *group)
{
    VFIOContainer *container = group->container;
    VFIOContainerBase *bcontainer = &container->bcontainer;
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    QLIST_REMOVE(group, container_next);
    group->container = NULL;

    /*
     * Explicitly release the listener first before unset container,
     * since unset may destroy the backend container if it's the last
     * group.
     */
    if (QLIST_EMPTY(&container->group_list)) {
        memory_listener_unregister(&bcontainer->listener);
        if (vioc->release) {
            vioc->release(bcontainer);
        }
    }

    if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd)) {
        error_report("vfio: error disconnecting group %d from container",
                     group->groupid);
    }

    if (QLIST_EMPTY(&container->group_list)) {
        VFIOAddressSpace *space = bcontainer->space;

        trace_vfio_disconnect_container(container->fd);
        vfio_cpr_unregister_container(bcontainer);
        close(container->fd);
        object_unref(container);

        vfio_put_address_space(space);
    }
}

static VFIOGroup *vfio_get_group(int groupid, AddressSpace *as, Error **errp)
{
    ERRP_GUARD();
    VFIOGroup *group;
    char path[32];
    struct vfio_group_status status = { .argsz = sizeof(status) };

    QLIST_FOREACH(group, &vfio_group_list, next) {
        if (group->groupid == groupid) {
            /* Found it.  Now is it already in the right context? */
            if (group->container->bcontainer.space->as == as) {
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
    group->fd = qemu_open(path, O_RDWR, errp);
    if (group->fd < 0) {
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

    if (!vfio_connect_container(group, as, errp)) {
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

static bool vfio_get_device(VFIOGroup *group, const char *name,
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
        return false;
    }

    info = vfio_get_device_info(fd);
    if (!info) {
        error_setg_errno(errp, errno, "error getting device info");
        close(fd);
        return false;
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
            return false;
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

    return true;
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
    char *tmp, group_path[PATH_MAX];
    g_autofree char *group_name = NULL;
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

    group_name = g_path_get_basename(group_path);
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
static bool vfio_legacy_attach_device(const char *name, VFIODevice *vbasedev,
                                      AddressSpace *as, Error **errp)
{
    int groupid = vfio_device_groupid(vbasedev, errp);
    VFIODevice *vbasedev_iter;
    VFIOGroup *group;
    VFIOContainerBase *bcontainer;

    if (groupid < 0) {
        return false;
    }

    trace_vfio_attach_device(vbasedev->name, groupid);

    if (!vfio_device_hiod_realize(vbasedev, errp)) {
        return false;
    }

    group = vfio_get_group(groupid, as, errp);
    if (!group) {
        return false;
    }

    QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
        if (strcmp(vbasedev_iter->name, vbasedev->name) == 0) {
            error_setg(errp, "device is already attached");
            vfio_put_group(group);
            return false;
        }
    }
    if (!vfio_get_device(group, name, vbasedev, errp)) {
        vfio_put_group(group);
        return false;
    }

    bcontainer = &group->container->bcontainer;
    vbasedev->bcontainer = bcontainer;
    QLIST_INSERT_HEAD(&bcontainer->device_list, vbasedev, container_next);
    QLIST_INSERT_HEAD(&vfio_device_list, vbasedev, global_next);

    return true;
}

static void vfio_legacy_detach_device(VFIODevice *vbasedev)
{
    VFIOGroup *group = vbasedev->group;

    QLIST_REMOVE(vbasedev, global_next);
    QLIST_REMOVE(vbasedev, container_next);
    vbasedev->bcontainer = NULL;
    trace_vfio_detach_device(vbasedev->name, group->groupid);
    vfio_put_base_device(vbasedev);
    vfio_put_group(group);
}

static int vfio_legacy_pci_hot_reset(VFIODevice *vbasedev, bool single)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    VFIOGroup *group;
    struct vfio_pci_hot_reset_info *info = NULL;
    struct vfio_pci_dependent_device *devices;
    struct vfio_pci_hot_reset *reset;
    int32_t *fds;
    int ret, i, count;
    bool multi = false;

    trace_vfio_pci_hot_reset(vdev->vbasedev.name, single ? "one" : "multi");

    if (!single) {
        vfio_pci_pre_reset(vdev);
    }
    vdev->vbasedev.needs_reset = false;

    ret = vfio_pci_get_pci_hot_reset_info(vdev, &info);

    if (ret) {
        goto out_single;
    }
    devices = &info->devices[0];

    trace_vfio_pci_hot_reset_has_dep_devices(vdev->vbasedev.name);

    /* Verify that we have all the groups required */
    for (i = 0; i < info->count; i++) {
        PCIHostDeviceAddress host;
        VFIOPCIDevice *tmp;
        VFIODevice *vbasedev_iter;

        host.domain = devices[i].segment;
        host.bus = devices[i].bus;
        host.slot = PCI_SLOT(devices[i].devfn);
        host.function = PCI_FUNC(devices[i].devfn);

        trace_vfio_pci_hot_reset_dep_devices(host.domain,
                host.bus, host.slot, host.function, devices[i].group_id);

        if (vfio_pci_host_match(&host, vdev->vbasedev.name)) {
            continue;
        }

        QLIST_FOREACH(group, &vfio_group_list, next) {
            if (group->groupid == devices[i].group_id) {
                break;
            }
        }

        if (!group) {
            if (!vdev->has_pm_reset) {
                error_report("vfio: Cannot reset device %s, "
                             "depends on group %d which is not owned.",
                             vdev->vbasedev.name, devices[i].group_id);
            }
            ret = -EPERM;
            goto out;
        }

        /* Prep dependent devices for reset and clear our marker. */
        QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
            if (!vbasedev_iter->dev->realized ||
                vbasedev_iter->type != VFIO_DEVICE_TYPE_PCI) {
                continue;
            }
            tmp = container_of(vbasedev_iter, VFIOPCIDevice, vbasedev);
            if (vfio_pci_host_match(&host, tmp->vbasedev.name)) {
                if (single) {
                    ret = -EINVAL;
                    goto out_single;
                }
                vfio_pci_pre_reset(tmp);
                tmp->vbasedev.needs_reset = false;
                multi = true;
                break;
            }
        }
    }

    if (!single && !multi) {
        ret = -EINVAL;
        goto out_single;
    }

    /* Determine how many group fds need to be passed */
    count = 0;
    QLIST_FOREACH(group, &vfio_group_list, next) {
        for (i = 0; i < info->count; i++) {
            if (group->groupid == devices[i].group_id) {
                count++;
                break;
            }
        }
    }

    reset = g_malloc0(sizeof(*reset) + (count * sizeof(*fds)));
    reset->argsz = sizeof(*reset) + (count * sizeof(*fds));
    fds = &reset->group_fds[0];

    /* Fill in group fds */
    QLIST_FOREACH(group, &vfio_group_list, next) {
        for (i = 0; i < info->count; i++) {
            if (group->groupid == devices[i].group_id) {
                fds[reset->count++] = group->fd;
                break;
            }
        }
    }

    /* Bus reset! */
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_PCI_HOT_RESET, reset);
    g_free(reset);
    if (ret) {
        ret = -errno;
    }

    trace_vfio_pci_hot_reset_result(vdev->vbasedev.name,
                                    ret ? strerror(errno) : "Success");

out:
    /* Re-enable INTx on affected devices */
    for (i = 0; i < info->count; i++) {
        PCIHostDeviceAddress host;
        VFIOPCIDevice *tmp;
        VFIODevice *vbasedev_iter;

        host.domain = devices[i].segment;
        host.bus = devices[i].bus;
        host.slot = PCI_SLOT(devices[i].devfn);
        host.function = PCI_FUNC(devices[i].devfn);

        if (vfio_pci_host_match(&host, vdev->vbasedev.name)) {
            continue;
        }

        QLIST_FOREACH(group, &vfio_group_list, next) {
            if (group->groupid == devices[i].group_id) {
                break;
            }
        }

        if (!group) {
            break;
        }

        QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
            if (!vbasedev_iter->dev->realized ||
                vbasedev_iter->type != VFIO_DEVICE_TYPE_PCI) {
                continue;
            }
            tmp = container_of(vbasedev_iter, VFIOPCIDevice, vbasedev);
            if (vfio_pci_host_match(&host, tmp->vbasedev.name)) {
                vfio_pci_post_reset(tmp);
                break;
            }
        }
    }
out_single:
    if (!single) {
        vfio_pci_post_reset(vdev);
    }
    g_free(info);

    return ret;
}

static void vfio_iommu_legacy_class_init(ObjectClass *klass, void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

    vioc->hiod_typename = TYPE_HOST_IOMMU_DEVICE_LEGACY_VFIO;

    vioc->setup = vfio_legacy_setup;
    vioc->dma_map = vfio_legacy_dma_map;
    vioc->dma_unmap = vfio_legacy_dma_unmap;
    vioc->attach_device = vfio_legacy_attach_device;
    vioc->detach_device = vfio_legacy_detach_device;
    vioc->set_dirty_page_tracking = vfio_legacy_set_dirty_page_tracking;
    vioc->query_dirty_bitmap = vfio_legacy_query_dirty_bitmap;
    vioc->pci_hot_reset = vfio_legacy_pci_hot_reset;
};

static bool hiod_legacy_vfio_realize(HostIOMMUDevice *hiod, void *opaque,
                                     Error **errp)
{
    VFIODevice *vdev = opaque;

    hiod->name = g_strdup(vdev->name);
    hiod->agent = opaque;

    return true;
}

static int hiod_legacy_vfio_get_cap(HostIOMMUDevice *hiod, int cap,
                                    Error **errp)
{
    switch (cap) {
    case HOST_IOMMU_DEVICE_CAP_AW_BITS:
        return vfio_device_get_aw_bits(hiod->agent);
    default:
        error_setg(errp, "%s: unsupported capability %x", hiod->name, cap);
        return -EINVAL;
    }
}

static GList *
hiod_legacy_vfio_get_iova_ranges(HostIOMMUDevice *hiod)
{
    VFIODevice *vdev = hiod->agent;

    g_assert(vdev);
    return vfio_container_get_iova_ranges(vdev->bcontainer);
}

static uint64_t
hiod_legacy_vfio_get_page_size_mask(HostIOMMUDevice *hiod)
{
    VFIODevice *vdev = hiod->agent;

    g_assert(vdev);
    return vfio_container_get_page_size_mask(vdev->bcontainer);
}

static void vfio_iommu_legacy_instance_init(Object *obj)
{
    VFIOContainer *container = VFIO_IOMMU_LEGACY(obj);

    QLIST_INIT(&container->group_list);
}

static void hiod_legacy_vfio_class_init(ObjectClass *oc, void *data)
{
    HostIOMMUDeviceClass *hioc = HOST_IOMMU_DEVICE_CLASS(oc);

    hioc->realize = hiod_legacy_vfio_realize;
    hioc->get_cap = hiod_legacy_vfio_get_cap;
    hioc->get_iova_ranges = hiod_legacy_vfio_get_iova_ranges;
    hioc->get_page_size_mask = hiod_legacy_vfio_get_page_size_mask;
};

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU_LEGACY,
        .parent = TYPE_VFIO_IOMMU,
        .instance_init = vfio_iommu_legacy_instance_init,
        .instance_size = sizeof(VFIOContainer),
        .class_init = vfio_iommu_legacy_class_init,
    }, {
        .name = TYPE_HOST_IOMMU_DEVICE_LEGACY_VFIO,
        .parent = TYPE_HOST_IOMMU_DEVICE,
        .class_init = hiod_legacy_vfio_class_init,
    }
};

DEFINE_TYPES(types)
