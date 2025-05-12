/*
 * Copyright (c) 2021-2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qemu/osdep.h"
#include "hw/vfio/vfio-container.h"
#include "hw/vfio/vfio-cpr.h"
#include "hw/vfio/vfio-device.h"
#include "migration/blocker.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

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

static int vfio_container_post_load(void *opaque, int version_id)
{
    VFIOContainer *container = opaque;
    VFIOGroup *group;
    VFIODevice *vbasedev;

    container->cpr.reused = false;

    QLIST_FOREACH(group, &container->group_list, container_next) {
        QLIST_FOREACH(vbasedev, &group->device_list, next) {
            vbasedev->cpr.reused = false;
        }
    }
    return 0;
}

static const VMStateDescription vfio_container_vmstate = {
    .name = "vfio-container",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vfio_container_post_load,
    .needed = cpr_needed_for_reuse,
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

    return true;
}

void vfio_legacy_cpr_unregister_container(VFIOContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;

    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
    migrate_del_blocker(&container->cpr.blocker);
    vmstate_unregister(NULL, &vfio_container_vmstate, container);
}

static bool same_device(int fd1, int fd2)
{
    struct stat st1, st2;

    return !fstat(fd1, &st1) && !fstat(fd2, &st2) && st1.st_dev == st2.st_dev;
}

bool vfio_cpr_container_match(VFIOContainer *container, VFIOGroup *group,
                              int *pfd)
{
    if (container->fd == *pfd) {
        return true;
    }
    if (!same_device(container->fd, *pfd)) {
        return false;
    }
    /*
     * Same device, different fd.  This occurs when the container fd is
     * cpr_save'd multiple times, once for each groupid, so SCM_RIGHTS
     * produces duplicates.  De-dup it.
     */
    cpr_delete_fd("vfio_container_for_group", group->groupid);
    close(*pfd);
    cpr_save_fd("vfio_container_for_group", group->groupid, container->fd);
    *pfd = container->fd;
    return true;
}
