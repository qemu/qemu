/*
 * Copyright (c) 2021-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qemu/osdep.h"
#include "hw/vfio/vfio-container.h"
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

static const VMStateDescription vfio_container_vmstate = {
    .name = "vfio-container",
    .version_id = 0,
    .minimum_version_id = 0,
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

    return true;
}

void vfio_legacy_cpr_unregister_container(VFIOContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;

    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
    migrate_del_blocker(&container->cpr.blocker);
    vmstate_unregister(NULL, &vfio_container_vmstate, container);
}
