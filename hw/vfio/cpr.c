/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/vfio-cpr.h"
#include "hw/vfio/pci.h"
#include "migration/cpr.h"
#include "qapi/error.h"
#include "system/runstate.h"

int vfio_cpr_reboot_notifier(NotifierWithReturn *notifier,
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

bool vfio_cpr_register_container(VFIOContainerBase *bcontainer, Error **errp)
{
    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);
    return true;
}

void vfio_cpr_unregister_container(VFIOContainerBase *bcontainer)
{
    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
}

/*
 * The kernel may change non-emulated config bits.  Exclude them from the
 * changed-bits check in get_pci_config_device.
 */
static int vfio_cpr_pci_pre_load(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;
    PCIDevice *pdev = &vdev->pdev;
    int size = MIN(pci_config_size(pdev), vdev->config_size);
    int i;

    for (i = 0; i < size; i++) {
        pdev->cmask[i] &= vdev->emulated_config_bits[i];
    }

    return 0;
}

const VMStateDescription vfio_cpr_pci_vmstate = {
    .name = "vfio-cpr-pci",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = vfio_cpr_pci_pre_load,
    .needed = cpr_incoming_needed,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};
