/*
 * Copyright (c) 2024-2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/vfio/vfio-cpr.h"
#include "hw/vfio/vfio-device.h"
#include "migration/blocker.h"
#include "migration/cpr.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "system/iommufd.h"
#include "vfio-iommufd.h"
#include "trace.h"

typedef struct CprVFIODevice {
    char *name;
    unsigned int namelen;
    uint32_t ioas_id;
    int devid;
    uint32_t hwpt_id;
    QLIST_ENTRY(CprVFIODevice) next;
} CprVFIODevice;

static const VMStateDescription vmstate_cpr_vfio_device = {
    .name = "cpr vfio device",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(namelen, CprVFIODevice),
        VMSTATE_VBUFFER_ALLOC_UINT32(name, CprVFIODevice, 0, NULL, namelen),
        VMSTATE_INT32(devid, CprVFIODevice),
        VMSTATE_UINT32(ioas_id, CprVFIODevice),
        VMSTATE_UINT32(hwpt_id, CprVFIODevice),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_cpr_vfio_devices = {
    .name = CPR_STATE "/vfio devices",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_QLIST_V(vfio_devices, CprState, 1, vmstate_cpr_vfio_device,
                        CprVFIODevice, next),
        VMSTATE_END_OF_LIST()
    }
};

static void vfio_cpr_save_device(VFIODevice *vbasedev)
{
    CprVFIODevice *elem = g_new0(CprVFIODevice, 1);

    elem->name = g_strdup(vbasedev->name);
    elem->namelen = strlen(vbasedev->name) + 1;
    elem->ioas_id = vbasedev->cpr.ioas_id;
    elem->devid = vbasedev->devid;
    elem->hwpt_id = vbasedev->cpr.hwpt_id;
    QLIST_INSERT_HEAD(&cpr_state.vfio_devices, elem, next);
}

static CprVFIODevice *find_device(const char *name)
{
    CprVFIODeviceList *head = &cpr_state.vfio_devices;
    CprVFIODevice *elem;

    QLIST_FOREACH(elem, head, next) {
        if (!strcmp(elem->name, name)) {
            return elem;
        }
    }
    return NULL;
}

static void vfio_cpr_delete_device(const char *name)
{
    CprVFIODevice *elem = find_device(name);

    if (elem) {
        QLIST_REMOVE(elem, next);
        g_free(elem->name);
        g_free(elem);
    }
}

static bool vfio_cpr_find_device(VFIODevice *vbasedev)
{
    CprVFIODevice *elem = find_device(vbasedev->name);

    if (elem) {
        vbasedev->cpr.ioas_id = elem->ioas_id;
        vbasedev->devid = elem->devid;
        vbasedev->cpr.hwpt_id = elem->hwpt_id;
        trace_vfio_cpr_find_device(elem->ioas_id, elem->devid, elem->hwpt_id);
        return true;
    }
    return false;
}

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

static int iommufd_cpr_pre_save(void *opaque)
{
    IOMMUFDBackend *be = opaque;

    /*
     * The process has not changed yet, but proactively try the ioctl,
     * and it will fail if any DMA mappings are not supported.
     */
    if (!iommufd_change_process_capable(be)) {
        error_report("some memory regions do not support "
                     "IOMMU_IOAS_CHANGE_PROCESS");
        return -1;
    }
    return 0;
}

static int iommufd_cpr_post_load(void *opaque, int version_id)
{
     IOMMUFDBackend *be = opaque;
     Error *local_err = NULL;

     if (!iommufd_change_process(be, &local_err)) {
        error_report_err(local_err);
        return -1;
     }
     return 0;
}

static const VMStateDescription iommufd_cpr_vmstate = {
    .name = "iommufd",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = iommufd_cpr_pre_save,
    .post_load = iommufd_cpr_post_load,
    .needed = cpr_incoming_needed,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

bool vfio_iommufd_cpr_register_iommufd(IOMMUFDBackend *be, Error **errp)
{
    Error **cpr_blocker = &be->cpr_blocker;

    if (!vfio_cpr_supported(be, cpr_blocker)) {
        return migrate_add_blocker_modes(cpr_blocker,
                    BIT(MIG_MODE_CPR_TRANSFER) | BIT(MIG_MODE_CPR_TRANSFER),
                    errp);
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
    VFIOContainer *bcontainer = VFIO_IOMMU(container);

    migration_add_notifier_mode(&bcontainer->cpr_reboot_notifier,
                                vfio_cpr_reboot_notifier,
                                MIG_MODE_CPR_REBOOT);

    vfio_cpr_add_kvm_notifier();

    return true;
}

void vfio_iommufd_cpr_unregister_container(VFIOIOMMUFDContainer *container)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(container);

    migration_remove_notifier(&bcontainer->cpr_reboot_notifier);
}

void vfio_iommufd_cpr_register_device(VFIODevice *vbasedev)
{
    if (!cpr_is_incoming()) {
        /*
         * Beware fd may have already been saved by vfio_device_set_fd,
         * so call resave to avoid a duplicate entry.
         */
        cpr_resave_fd(vbasedev->name, 0, vbasedev->fd);
        vfio_cpr_save_device(vbasedev);
    }
}

void vfio_iommufd_cpr_unregister_device(VFIODevice *vbasedev)
{
    cpr_delete_fd(vbasedev->name, 0);
    vfio_cpr_delete_device(vbasedev->name);
}

void vfio_cpr_load_device(VFIODevice *vbasedev)
{
    if (cpr_is_incoming()) {
        bool ret = vfio_cpr_find_device(vbasedev);
        g_assert(ret);

        if (vbasedev->fd < 0) {
            vbasedev->fd = cpr_find_fd(vbasedev->name, 0);
        }
    }
}
