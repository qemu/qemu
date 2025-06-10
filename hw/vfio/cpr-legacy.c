/*
 * Copyright (c) 2021-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qemu/osdep.h"
#include "hw/vfio/vfio-container.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/vfio-listener.h"
#include "migration/blocker.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

static bool vfio_dma_unmap_vaddr_all(VFIOContainer *container, Error **errp)
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
    return true;
}

/*
 * Set the new @vaddr for any mappings registered during cpr load.
 * The incoming state is cleared thereafter.
 */
static int vfio_legacy_cpr_dma_map(const VFIOContainerBase *bcontainer,
                                   hwaddr iova, ram_addr_t size, void *vaddr,
                                   bool readonly, MemoryRegion *mr)
{
    const VFIOContainer *container = container_of(bcontainer, VFIOContainer,
                                                  bcontainer);
    struct vfio_iommu_type1_dma_map map = {
        .argsz = sizeof(map),
        .flags = VFIO_DMA_MAP_FLAG_VADDR,
        .vaddr = (__u64)(uintptr_t)vaddr,
        .iova = iova,
        .size = size,
    };

    g_assert(cpr_is_incoming());

    if (ioctl(container->fd, VFIO_IOMMU_MAP_DMA, &map)) {
        return -errno;
    }

    return 0;
}

static bool vfio_cpr_supported(VFIOContainer *container, Error **errp)
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
    VFIOContainer *container = opaque;
    Error *local_err = NULL;

    if (!vfio_dma_unmap_vaddr_all(container, &local_err)) {
        error_report_err(local_err);
        return -1;
    }
    return 0;
}

static int vfio_container_post_load(void *opaque, int version_id)
{
    VFIOContainer *container = opaque;
    VFIOContainerBase *bcontainer = &container->bcontainer;
    VFIOGroup *group;
    Error *local_err = NULL;

    if (!vfio_listener_register(bcontainer, &local_err)) {
        error_report_err(local_err);
        return -1;
    }

    QLIST_FOREACH(group, &container->group_list, container_next) {
        VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);

        /* Restore original dma_map function */
        vioc->dma_map = container->cpr.saved_dma_map;
    }
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

bool vfio_legacy_cpr_register_container(VFIOContainer *container, Error **errp)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;
    Error **cpr_blocker = &container->cpr.blocker;

    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);

    if (!vfio_cpr_supported(container, cpr_blocker)) {
        return migrate_add_blocker_modes(cpr_blocker, errp,
                                         MIG_MODE_CPR_TRANSFER, -1) == 0;
    }

    vmstate_register(NULL, -1, &vfio_container_vmstate, container);

    /* During incoming CPR, divert calls to dma_map. */
    if (cpr_is_incoming()) {
        VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
        container->cpr.saved_dma_map = vioc->dma_map;
        vioc->dma_map = vfio_legacy_cpr_dma_map;
    }
    return true;
}

void vfio_legacy_cpr_unregister_container(VFIOContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;

    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
    migrate_del_blocker(&container->cpr.blocker);
    vmstate_unregister(NULL, &vfio_container_vmstate, container);
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

bool vfio_cpr_container_match(VFIOContainer *container, VFIOGroup *group,
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
