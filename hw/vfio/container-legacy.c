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

#include "hw/vfio/vfio-device.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/physmem.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "system/reset.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/cpr.h"
#include "migration/blocker.h"
#include "pci.h"
#include "hw/vfio/vfio-container-legacy.h"
#include "vfio-helpers.h"
#include "vfio-listener.h"

#define TYPE_HOST_IOMMU_DEVICE_LEGACY_VFIO TYPE_HOST_IOMMU_DEVICE "-legacy-vfio"

typedef QLIST_HEAD(VFIOGroupList, VFIOGroup) VFIOGroupList;
static VFIOGroupList vfio_group_list =
    QLIST_HEAD_INITIALIZER(vfio_group_list);

static int vfio_ram_block_discard_disable(VFIOLegacyContainer *container,
                                          bool state)
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

static int vfio_dma_unmap_bitmap(const VFIOLegacyContainer *container,
                                 hwaddr iova, uint64_t size,
                                 IOMMUTLBEntry *iotlb)
{
    const VFIOContainer *bcontainer = VFIO_IOMMU(container);
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
     * physical_memory_set_dirty_lebitmap() supports pages in bitmap of
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
        physical_memory_set_dirty_lebitmap(vbmap.bitmap,
                iotlb->translated_addr, vbmap.pages);
    } else {
        error_report("VFIO_UNMAP_DMA with DIRTY_BITMAP : %m");
    }

unmap_exit:
    g_free(unmap);
    g_free(vbmap.bitmap);

    return ret;
}

static int vfio_legacy_dma_unmap_one(const VFIOLegacyContainer *container,
                                     hwaddr iova, uint64_t size,
                                     uint32_t flags, IOMMUTLBEntry *iotlb)
{
    const VFIOContainer *bcontainer = VFIO_IOMMU(container);
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = flags,
        .iova = iova,
        .size = size,
    };
    bool need_dirty_sync = false;
    int ret;
    Error *local_err = NULL;

    g_assert(!cpr_is_incoming());

    if (iotlb && vfio_container_dirty_tracking_is_started(bcontainer)) {
        if (!vfio_container_devices_dirty_tracking_is_supported(bcontainer) &&
            bcontainer->dirty_pages_supported) {
            return vfio_dma_unmap_bitmap(container, iova, size, iotlb);
        }

        need_dirty_sync = true;
    }

    if (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        return -errno;
    }

    if (need_dirty_sync) {
        ret = vfio_container_query_dirty_bitmap(bcontainer, iova, size,
                                    iotlb->translated_addr, &local_err);
        if (ret) {
            error_report_err(local_err);
            return ret;
        }
    }

    return 0;
}

/*
 * DMA - Mapping and unmapping for the "type1" IOMMU interface used on x86
 */
static int vfio_legacy_dma_unmap(const VFIOContainer *bcontainer,
                                 hwaddr iova, uint64_t size,
                                 IOMMUTLBEntry *iotlb, bool unmap_all)
{
    const VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(bcontainer);
    uint32_t flags = 0;
    int ret;

    if (unmap_all) {
        if (container->unmap_all_supported) {
            flags = VFIO_DMA_UNMAP_FLAG_ALL;
        } else {
            /* The unmap ioctl doesn't accept a full 64-bit span. */
            Int128 llsize = int128_rshift(int128_2_64(), 1);
            size = int128_get64(llsize);

            ret = vfio_legacy_dma_unmap_one(container, 0, size, flags, iotlb);
            if (ret) {
                return ret;
            }

            iova = size;
        }
    }

    return vfio_legacy_dma_unmap_one(container, iova, size, flags, iotlb);
}

static int vfio_legacy_dma_map(const VFIOContainer *bcontainer, hwaddr iova,
                               uint64_t size, void *vaddr, bool readonly,
                               MemoryRegion *mr)
{
    const VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(bcontainer);
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
         vfio_legacy_dma_unmap(bcontainer, iova, size, NULL, false) == 0 &&
         ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map) == 0)) {
        return 0;
    }

    return -errno;
}

static int
vfio_legacy_set_dirty_page_tracking(const VFIOContainer *bcontainer,
                                    bool start, Error **errp)
{
    const VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(bcontainer);
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

static int vfio_legacy_query_dirty_bitmap(const VFIOContainer *bcontainer,
                      VFIOBitmap *vbmap, hwaddr iova, hwaddr size, Error **errp)
{
    const VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(bcontainer);
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
     * physical_memory_set_dirty_lebitmap() supports pages in bitmap of
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

static bool vfio_get_info_iova_range(struct vfio_iommu_type1_info *info,
                                     VFIOContainer *bcontainer)
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

static void vfio_group_add_kvm_device(VFIOGroup *group)
{
    Error *err = NULL;

    if (vfio_kvm_device_add_fd(group->fd, &err)) {
        error_reportf_err(err, "group ID %d: ", group->groupid);
    }
}

static void vfio_group_del_kvm_device(VFIOGroup *group)
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

static VFIOLegacyContainer *vfio_create_container(int fd, VFIOGroup *group,
                                            Error **errp)
{
    int iommu_type;
    const char *vioc_name;
    VFIOLegacyContainer *container;

    iommu_type = vfio_get_iommu_type(fd, errp);
    if (iommu_type < 0) {
        return NULL;
    }

    /*
     * During CPR, just set the container type and skip the ioctls, as the
     * container and group are already configured in the kernel.
     */
    if (!cpr_is_incoming() &&
        !vfio_set_iommu(fd, group->fd, &iommu_type, errp)) {
        return NULL;
    }

    vioc_name = vfio_get_iommu_class_name(iommu_type);

    container = VFIO_IOMMU_LEGACY(object_new(vioc_name));
    container->fd = fd;
    container->iommu_type = iommu_type;
    return container;
}

static int vfio_get_iommu_info(VFIOLegacyContainer *container,
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

static void vfio_get_iommu_info_migration(VFIOLegacyContainer *container,
                                          struct vfio_iommu_type1_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_iommu_type1_info_cap_migration *cap_mig;
    VFIOContainer *bcontainer = VFIO_IOMMU(container);

    hdr = vfio_get_iommu_info_cap(info, VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION);
    if (!hdr) {
        return;
    }

    cap_mig = container_of(hdr, struct vfio_iommu_type1_info_cap_migration,
                            header);

    /*
     * physical_memory_set_dirty_lebitmap() supports pages in bitmap of
     * qemu_real_host_page_size to mark those dirty.
     */
    if (cap_mig->pgsize_bitmap & qemu_real_host_page_size()) {
        bcontainer->dirty_pages_supported = true;
        bcontainer->max_dirty_bitmap_size = cap_mig->max_dirty_bitmap_size;
        bcontainer->dirty_pgsizes = cap_mig->pgsize_bitmap;
    }
}

static bool vfio_legacy_setup(VFIOContainer *bcontainer, Error **errp)
{
    VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(bcontainer);
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

    ret = ioctl(container->fd, VFIO_CHECK_EXTENSION, VFIO_UNMAP_ALL);
    container->unmap_all_supported = !!ret;

    vfio_get_iommu_info_migration(container, info);
    return true;
}

static bool vfio_container_attach_discard_disable(
    VFIOLegacyContainer *container, VFIOGroup *group, Error **errp)
{
    int ret;

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

    ret = vfio_ram_block_discard_disable(container, true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        if (ioctl(group->fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd)) {
            error_report("vfio: error disconnecting group %d from"
                         " container", group->groupid);
        }
    }
    return !ret;
}

static bool vfio_container_group_add(VFIOLegacyContainer *container,
                                     VFIOGroup *group, Error **errp)
{
    if (!vfio_container_attach_discard_disable(container, group, errp)) {
        return false;
    }
    group->container = container;
    QLIST_INSERT_HEAD(&container->group_list, group, container_next);
    vfio_group_add_kvm_device(group);
    /*
     * Remember the container fd for each group, so we can attach to the same
     * container after CPR.
     */
    cpr_resave_fd("vfio_container_for_group", group->groupid, container->fd);
    return true;
}

static void vfio_container_group_del(VFIOLegacyContainer *container,
                                     VFIOGroup *group)
{
    QLIST_REMOVE(group, container_next);
    group->container = NULL;
    vfio_group_del_kvm_device(group);
    vfio_ram_block_discard_disable(container, false);
    cpr_delete_fd("vfio_container_for_group", group->groupid);
}

static bool vfio_container_connect(VFIOGroup *group, AddressSpace *as,
                                   Error **errp)
{
    VFIOLegacyContainer *container;
    VFIOContainer *bcontainer;
    int ret, fd = -1;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc = NULL;
    bool new_container = false;
    bool group_was_added = false;

    space = vfio_address_space_get(as);
    fd = cpr_find_fd("vfio_container_for_group", group->groupid);

    if (!cpr_is_incoming()) {
        QLIST_FOREACH(bcontainer, &space->containers, next) {
            container = VFIO_IOMMU_LEGACY(bcontainer);
            if (!ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &container->fd)) {
                return vfio_container_group_add(container, group, errp);
            }
        }

        fd = qemu_open("/dev/vfio/vfio", O_RDWR, errp);
        if (fd < 0) {
            goto fail;
        }
    } else {
        /*
         * For incoming CPR, the group is already attached in the kernel.
         * If a container with matching fd is found, then update the
         * userland group list and return.  If not, then after the loop,
         * create the container struct and group list.
         */
        QLIST_FOREACH(bcontainer, &space->containers, next) {
            container = VFIO_IOMMU_LEGACY(bcontainer);

            if (vfio_cpr_container_match(container, group, fd)) {
                return vfio_container_group_add(container, group, errp);
            }
        }
    }

    ret = ioctl(fd, VFIO_GET_API_VERSION);
    if (ret != VFIO_API_VERSION) {
        error_setg(errp, "supported vfio version: %d, "
                   "reported version: %d", VFIO_API_VERSION, ret);
        goto fail;
    }

    container = vfio_create_container(fd, group, errp);
    if (!container) {
        goto fail;
    }
    new_container = true;
    bcontainer = VFIO_IOMMU(container);

    if (!vfio_legacy_cpr_register_container(container, errp)) {
        goto fail;
    }

    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    assert(vioc->setup);

    if (!vioc->setup(bcontainer, errp)) {
        goto fail;
    }

    vfio_address_space_insert(space, bcontainer);

    if (!vfio_container_group_add(container, group, errp)) {
        goto fail;
    }
    group_was_added = true;

    /*
     * If CPR, register the listener later, after all state that may
     * affect regions and mapping boundaries has been cpr load'ed.  Later,
     * the listener will invoke its callback on each flat section and call
     * dma_map to supply the new vaddr, and the calls will match the mappings
     * remembered by the kernel.
     */
    if (!cpr_is_incoming()) {
        if (!vfio_listener_register(bcontainer, errp)) {
            goto fail;
        }
    }

    bcontainer->initialized = true;

    return true;

fail:
    if (new_container) {
        vfio_listener_unregister(bcontainer);
    }

    if (group_was_added) {
        vfio_container_group_del(container, group);
    }
    if (vioc && vioc->release) {
        vioc->release(bcontainer);
    }
    if (new_container) {
        vfio_legacy_cpr_unregister_container(container);
        object_unref(container);
    }
    if (fd >= 0) {
        close(fd);
    }
    vfio_address_space_put(space);

    return false;
}

static void vfio_container_disconnect(VFIOGroup *group)
{
    VFIOLegacyContainer *container = group->container;
    VFIOContainer *bcontainer = VFIO_IOMMU(container);
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

    QLIST_REMOVE(group, container_next);
    group->container = NULL;
    cpr_delete_fd("vfio_container_for_group", group->groupid);

    /*
     * Explicitly release the listener first before unset container,
     * since unset may destroy the backend container if it's the last
     * group.
     */
    if (QLIST_EMPTY(&container->group_list)) {
        vfio_listener_unregister(bcontainer);
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

        trace_vfio_container_disconnect(container->fd);
        vfio_legacy_cpr_unregister_container(container);
        close(container->fd);
        object_unref(container);

        vfio_address_space_put(space);
    }
}

static VFIOGroup *vfio_group_get(int groupid, AddressSpace *as, Error **errp)
{
    ERRP_GUARD();
    VFIOGroup *group;
    char path[32];
    struct vfio_group_status status = { .argsz = sizeof(status) };

    QLIST_FOREACH(group, &vfio_group_list, next) {
        if (group->groupid == groupid) {
            /* Found it.  Now is it already in the right context? */
            if (VFIO_IOMMU(group->container)->space->as == as) {
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
    group->fd = cpr_open_fd(path, O_RDWR, "vfio_group", groupid, errp);
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

    if (!vfio_container_connect(group, as, errp)) {
        error_prepend(errp, "failed to setup container for group %d: ",
                      groupid);
        goto close_fd_exit;
    }

    QLIST_INSERT_HEAD(&vfio_group_list, group, next);

    return group;

close_fd_exit:
    cpr_delete_fd("vfio_group", groupid);
    close(group->fd);

free_group_exit:
    g_free(group);

    return NULL;
}

static void vfio_group_put(VFIOGroup *group)
{
    if (!group || !QLIST_EMPTY(&group->device_list)) {
        return;
    }

    if (!group->ram_block_discard_allowed) {
        vfio_ram_block_discard_disable(group->container, false);
    }
    vfio_group_del_kvm_device(group);
    vfio_container_disconnect(group);
    QLIST_REMOVE(group, next);
    trace_vfio_group_put(group->fd);
    cpr_delete_fd("vfio_group", group->groupid);
    close(group->fd);
    g_free(group);
}

static bool vfio_device_get(VFIOGroup *group, const char *name,
                            VFIODevice *vbasedev, Error **errp)
{
    g_autofree struct vfio_device_info *info = NULL;
    int fd;

    fd = vfio_cpr_group_get_device_fd(group->fd, name);
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
        goto fail;
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
            goto fail;
        }

        if (!group->ram_block_discard_allowed) {
            group->ram_block_discard_allowed = true;
            vfio_ram_block_discard_disable(group->container, false);
        }
    }

    vfio_device_prepare(vbasedev, VFIO_IOMMU(group->container), info);

    vbasedev->fd = fd;
    vbasedev->group = group;
    QLIST_INSERT_HEAD(&group->device_list, vbasedev, next);

    trace_vfio_device_get(name, info->flags, info->num_regions, info->num_irqs);

    return true;

fail:
    close(fd);
    cpr_delete_fd(name, 0);
    return false;
}

static void vfio_device_put(VFIODevice *vbasedev)
{
    if (!vbasedev->group) {
        return;
    }
    QLIST_REMOVE(vbasedev, next);
    vbasedev->group = NULL;
    trace_vfio_device_put(vbasedev->fd);
    cpr_delete_fd(vbasedev->name, 0);
    close(vbasedev->fd);
}

static int vfio_device_get_groupid(VFIODevice *vbasedev, Error **errp)
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
 * vfio_device_attach: attach a device to a security context
 * @name and @vbasedev->name are likely to be different depending
 * on the type of the device, hence the need for passing @name
 */
static bool vfio_legacy_attach_device(const char *name, VFIODevice *vbasedev,
                                      AddressSpace *as, Error **errp)
{
    int groupid = vfio_device_get_groupid(vbasedev, errp);
    VFIODevice *vbasedev_iter;
    VFIOGroup *group;

    if (groupid < 0) {
        return false;
    }

    trace_vfio_device_attach(vbasedev->name, groupid);

    group = vfio_group_get(groupid, as, errp);
    if (!group) {
        return false;
    }

    QLIST_FOREACH(vbasedev_iter, &group->device_list, next) {
        if (strcmp(vbasedev_iter->name, vbasedev->name) == 0) {
            error_setg(errp, "device is already attached");
            goto group_put_exit;
        }
    }
    if (!vfio_device_get(group, name, vbasedev, errp)) {
        goto group_put_exit;
    }

    if (!vfio_device_hiod_create_and_realize(vbasedev,
                                             TYPE_HOST_IOMMU_DEVICE_LEGACY_VFIO,
                                             errp)) {
        goto device_put_exit;
    }

    if (vbasedev->mdev) {
        error_setg(&vbasedev->cpr.mdev_blocker,
                   "CPR does not support vfio mdev %s", vbasedev->name);
        if (migrate_add_blocker_modes(&vbasedev->cpr.mdev_blocker,
                    BIT(MIG_MODE_CPR_TRANSFER) | BIT(MIG_MODE_CPR_EXEC),
                    errp) < 0) {
            goto hiod_unref_exit;
        }
    }

    return true;

hiod_unref_exit:
    object_unref(vbasedev->hiod);
device_put_exit:
    vfio_device_put(vbasedev);
group_put_exit:
    vfio_group_put(group);
    return false;
}

static void vfio_legacy_detach_device(VFIODevice *vbasedev)
{
    VFIOGroup *group = vbasedev->group;

    trace_vfio_device_detach(vbasedev->name, group->groupid);

    vfio_device_unprepare(vbasedev);

    migrate_del_blocker(&vbasedev->cpr.mdev_blocker);
    object_unref(vbasedev->hiod);
    vfio_device_put(vbasedev);
    vfio_group_put(group);
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
                !vfio_pci_from_vfio_device(vbasedev_iter)) {
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
                !vfio_pci_from_vfio_device(vbasedev_iter)) {
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

static void vfio_iommu_legacy_class_init(ObjectClass *klass, const void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

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
    VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(obj);

    QLIST_INIT(&container->group_list);
}

static void hiod_legacy_vfio_class_init(ObjectClass *oc, const void *data)
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
        .instance_size = sizeof(VFIOLegacyContainer),
        .class_init = vfio_iommu_legacy_class_init,
    }, {
        .name = TYPE_HOST_IOMMU_DEVICE_LEGACY_VFIO,
        .parent = TYPE_HOST_IOMMU_DEVICE,
        .class_init = hiod_legacy_vfio_class_init,
    }
};

DEFINE_TYPES(types)
