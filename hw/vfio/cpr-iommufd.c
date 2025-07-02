/*
 * Copyright (c) 2024-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/vfio/vfio-cpr.h"
#include "migration/blocker.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "system/iommufd.h"
#include "vfio-iommufd.h"

static bool vfio_cpr_supported(IOMMUFDBackend *be, Error **errp)
{
    if (!iommufd_change_process_capable(be)) {
        if (errp) {
            error_setg(errp, "vfio iommufd backend does not support "
                       "IOMMU_IOAS_CHANGE_PROCESS");
        }
        return false;
    }
    return true;
}

static const VMStateDescription iommufd_cpr_vmstate = {
    .name = "iommufd",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = cpr_incoming_needed,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

bool vfio_iommufd_cpr_register_iommufd(IOMMUFDBackend *be, Error **errp)
{
    Error **cpr_blocker = &be->cpr_blocker;

    if (!vfio_cpr_supported(be, cpr_blocker)) {
        return migrate_add_blocker_modes(cpr_blocker, errp,
                                         MIG_MODE_CPR_TRANSFER, -1) == 0;
    }

    vmstate_register(NULL, -1, &iommufd_cpr_vmstate, be);

    return true;
}

void vfio_iommufd_cpr_unregister_iommufd(IOMMUFDBackend *be)
{
    vmstate_unregister(NULL, &iommufd_cpr_vmstate, be);
    migrate_del_blocker(&be->cpr_blocker);
}

bool vfio_iommufd_cpr_register_container(VFIOIOMMUFDContainer *container,
                                         Error **errp)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;

    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);

    vfio_cpr_add_kvm_notifier();

    return true;
}

void vfio_iommufd_cpr_unregister_container(VFIOIOMMUFDContainer *container)
{
    VFIOContainerBase *bcontainer = &container->bcontainer;

    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
}

void vfio_iommufd_cpr_register_device(VFIODevice *vbasedev)
{
}

void vfio_iommufd_cpr_unregister_device(VFIODevice *vbasedev)
{
}
