/*
 * Copyright (c) 2021-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "hw/vfio/vfio-container-legacy.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/vfio-listener.h"
#include "migration/blocker.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

static bool vfio_dma_unmap_vaddr_all(VFIOLegacyContainer *container,
                                     Error **errp)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = VFIO_DMA_UNMAP_FLAG_VADDR | VFIO_DMA_UNMAP_FLAG_ALL,
        .iova = 0,
        .size = 0,
    };
    if (ioctl(container->fd, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        error_setg_errno(errp, errno, "vfio_dma_unmap_vaddr_all");
        return false;
    }
    container->cpr.vaddr_unmapped = true;
    return true;
}

/*
 * Set the new @vaddr for any mappings registered during cpr load.
 * The incoming state is cleared thereafter.
 */
static int vfio_legacy_cpr_dma_map(const VFIOContainer *bcontainer,
                                   hwaddr iova, uint64_t size, void *vaddr,
                                   bool readonly, MemoryRegion *mr)
{
    const VFIOLegacyContainer *container = VFIO_IOMMU_LEGACY(bcontainer);

    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .flags = VFIO_DMA_MAP_FLAG_VADDR,
        .vaddr = (__u64)(uintptr_t)vaddr,
        .iova = iova,
        .size = size,
    };

    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map)) {
        return -errno;
    }

    return 0;
}

static void vfio_region_remap(MemoryListener *listener,
                              MemoryRegionSection *section)
{
    VFIOLegacyContainer *container = container_of(listener,
                                                  VFIOLegacyContainer,
                                                  cpr.remap_listener);
    vfio_container_region_add(VFIO_IOMMU(container), section, true);
}

static bool vfio_cpr_supported(VFIOLegacyContainer *container, Error **errp)
{
    if (!ioctl(container->fd, VFIO_CHECK_EXTENSION, VFIO_UPDATE_VADDR)) {
        error_setg(errp, "VFIO container does not support VFIO_UPDATE_VADDR");
        return false;

    } else if (!ioctl(container->fd, VFIO_CHECK_EXTENSION, VFIO_UNMAP_ALL)) {
        error_setg(errp, "VFIO container does not support VFIO_UNMAP_ALL");
        return false;

    } else {
        return true;
    }
}

static int vfio_container_pre_save(void *opaque)
{
    VFIOLegacyContainer *container = opaque;
    Error *local_err = NULL;

    if (!vfio_dma_unmap_vaddr_all(container, &local_err)) {
        error_report_err(local_err);
        return -1;
    }
    return 0;
}

static int vfio_container_post_load(void *opaque, int version_id)
{
    VFIOLegacyContainer *container = opaque;
    VFIOContainer *bcontainer = VFIO_IOMMU(container);
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    dma_map_fn saved_dma_map = vioc->dma_map;
    Error *local_err = NULL;

    /* During incoming CPR, divert calls to dma_map. */
    vioc->dma_map = vfio_legacy_cpr_dma_map;

    if (!vfio_listener_register(bcontainer, &local_err)) {
        error_report_err(local_err);
        return -1;
    }

    /* Restore original dma_map function */
    vioc->dma_map = saved_dma_map;

    return 0;
}

static const VMStateDescription vfio_container_vmstate = {
    .name = "vfio-container",
    .version_id = 0,
    .minimum_version_id = 0,
    .priority = MIG_PRI_LOW,  /* Must happen after devices and groups */
    .pre_save = vfio_container_pre_save,
    .post_load = vfio_container_post_load,
    .needed = cpr_incoming_needed,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int vfio_cpr_fail_notifier(NotifierWithReturn *notifier,
                                  MigrationEvent *e, Error **errp)
{
    VFIOLegacyContainer *container =
        container_of(notifier, VFIOLegacyContainer, cpr.transfer_notifier);
    VFIOContainer *bcontainer = VFIO_IOMMU(container);

    if (e->type != MIG_EVENT_PRECOPY_FAILED) {
        return 0;
    }

    if (container->cpr.vaddr_unmapped) {
        /*
         * Force a call to vfio_region_remap for each mapped section by
         * temporarily registering a listener, and temporarily diverting
         * dma_map to vfio_legacy_cpr_dma_map.  The latter restores vaddr.
         */

        VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
        dma_map_fn saved_dma_map = vioc->dma_map;
        vioc->dma_map = vfio_legacy_cpr_dma_map;

        container->cpr.remap_listener = (MemoryListener) {
            .name = "vfio cpr recover",
            .region_add = vfio_region_remap
        };
        memory_listener_register(&container->cpr.remap_listener,
                                 bcontainer->space->as);
        memory_listener_unregister(&container->cpr.remap_listener);
        container->cpr.vaddr_unmapped = false;
        vioc->dma_map = saved_dma_map;
    }
    return 0;
}

bool vfio_legacy_cpr_register_container(VFIOLegacyContainer *container,
                                        Error **errp)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(container);
    Error **cpr_blocker = &container->cpr.blocker;

    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);

    if (!vfio_cpr_supported(container, cpr_blocker)) {
        return migrate_add_blocker_modes(cpr_blocker,
                        BIT(MIG_MODE_CPR_TRANSFER) | BIT(MIG_MODE_CPR_EXEC),
                        errp) == 0;
    }

    vfio_cpr_add_kvm_notifier();

    vmstate_register(NULL, -1, &vfio_container_vmstate, container);

    migration_add_notifier_modes(&container->cpr.transfer_notifier,
                                 vfio_cpr_fail_notifier,
                                 BIT(MIG_MODE_CPR_TRANSFER) | BIT(MIG_MODE_CPR_EXEC));
    return true;
}

void vfio_legacy_cpr_unregister_container(VFIOLegacyContainer *container)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(container);

    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
    migrate_del_blocker(&container->cpr.blocker);
    vmstate_unregister(NULL, &vfio_container_vmstate, container);
    migration_remove_notifier(&container->cpr.transfer_notifier);
}

/*
 * In old QEMU, VFIO_DMA_UNMAP_FLAG_VADDR may fail on some mapping after
 * succeeding for others, so the latter have lost their vaddr.  Call this
 * to restore vaddr for a section with a giommu.
 *
 * The giommu already exists.  Find it and replay it, which calls
 * vfio_legacy_cpr_dma_map further down the stack.
 */
void vfio_cpr_giommu_remap(VFIOContainer *bcontainer,
                           MemoryRegionSection *section)
{
    VFIOGuestIOMMU *giommu = NULL;
    hwaddr as_offset = section->offset_within_address_space;
    hwaddr iommu_offset = as_offset - section->offset_within_region;

    QLIST_FOREACH(giommu, &bcontainer->giommu_list, giommu_next) {
        if (giommu->iommu_mr == IOMMU_MEMORY_REGION(section->mr) &&
            giommu->iommu_offset == iommu_offset) {
            break;
        }
    }
    g_assert(giommu);
    memory_region_iommu_replay(giommu->iommu_mr, &giommu->n);
}

static int vfio_cpr_rdm_remap(MemoryRegionSection *section, void *opaque)
{
    RamDiscardListener *rdl = opaque;

    return rdl->notify_populate(rdl, section);
}

/*
 * In old QEMU, VFIO_DMA_UNMAP_FLAG_VADDR may fail on some mapping after
 * succeeding for others, so the latter have lost their vaddr.  Call this
 * to restore vaddr for populated parts in a section with a RamDiscardManager.
 *
 * The ram discard listener already exists.  Call its replay_populated function
 * directly, which calls vfio_legacy_cpr_dma_map.
 */
bool vfio_cpr_ram_discard_replay_populated(VFIOContainer *bcontainer,
                                           MemoryRegionSection *section)
{
    RamDiscardManager *rdm = memory_region_get_ram_discard_manager(section->mr);
    VFIORamDiscardListener *vrdl =
        vfio_find_ram_discard_listener(bcontainer, section);

    g_assert(vrdl);
    return ram_discard_manager_replay_populated(rdm, section,
                                                vfio_cpr_rdm_remap,
                                                &vrdl->listener) == 0;
}

int vfio_cpr_group_get_device_fd(int d, const char *name)
{
    const int id = 0;
    int fd = cpr_find_fd(name, id);

    if (fd < 0) {
        fd = ioctl(d, VFIO_GROUP_GET_DEVICE_FD, name);
        if (fd >= 0) {
            cpr_save_fd(name, id, fd);
        }
    }
    return fd;
}

static bool same_device(int fd1, int fd2)
{
    struct stat st1, st2;

    return !fstat(fd1, &st1) && !fstat(fd2, &st2) && st1.st_dev == st2.st_dev;
}

bool vfio_cpr_container_match(VFIOLegacyContainer *container, VFIOGroup *group,
                              int fd)
{
    if (container->fd == fd) {
        return true;
    }
    if (!same_device(container->fd, fd)) {
        return false;
    }
    /*
     * Same device, different fd.  This occurs when the container fd is
     * cpr_save'd multiple times, once for each groupid, so SCM_RIGHTS
     * produces duplicates.  De-dup it.
     */
    cpr_delete_fd("vfio_container_for_group", group->groupid);
    close(fd);
    cpr_save_fd("vfio_container_for_group", group->groupid, container->fd);
    return true;
}
