/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-common.h"
#include "migration/misc.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"

static int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier,
                                    MigrationEvent *e, Error **errp)
{
    if (e->type == MIG_EVENT_PRECOPY_SETUP &&
        !runstate_check(RUN_STATE_SUSPENDED) && !vm_get_suspended()) {

        error_setg(errp,
            "VFIO device only supports cpr-reboot for runstate suspended");

        return -1;
    }
    return 0;
}

int vfio_cpr_register_container(VFIOContainerBase *bcontainer, Error **errp)
{
    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);
    return 0;
}

void vfio_cpr_unregister_container(VFIOContainerBase *bcontainer)
{
    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
}
