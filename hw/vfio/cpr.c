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
#include "hw/pci/msix.h"
#include "hw/pci/msi.h"
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

#define STRDUP_VECTOR_FD_NAME(vdev, name)   \
    g_strdup_printf("%s_%s", (vdev)->vbasedev.name, (name))

void vfio_cpr_save_vector_fd(VFIOPCIDevice *vdev, const char *name, int nr,
                             int fd)
{
    g_autofree char *fdname = STRDUP_VECTOR_FD_NAME(vdev, name);
    cpr_save_fd(fdname, nr, fd);
}

int vfio_cpr_load_vector_fd(VFIOPCIDevice *vdev, const char *name, int nr)
{
    g_autofree char *fdname = STRDUP_VECTOR_FD_NAME(vdev, name);
    return cpr_find_fd(fdname, nr);
}

void vfio_cpr_delete_vector_fd(VFIOPCIDevice *vdev, const char *name, int nr)
{
    g_autofree char *fdname = STRDUP_VECTOR_FD_NAME(vdev, name);
    cpr_delete_fd(fdname, nr);
}

static void vfio_cpr_claim_vectors(VFIOPCIDevice *vdev, int nr_vectors,
                                   bool msix)
{
    int i, fd;
    bool pending = false;
    PCIDevice *pdev = &vdev->pdev;

    vdev->nr_vectors = nr_vectors;
    vdev->msi_vectors = g_new0(VFIOMSIVector, nr_vectors);
    vdev->interrupt = msix ? VFIO_INT_MSIX : VFIO_INT_MSI;

    vfio_pci_prepare_kvm_msi_virq_batch(vdev);

    for (i = 0; i < nr_vectors; i++) {
        VFIOMSIVector *vector = &vdev->msi_vectors[i];

        fd = vfio_cpr_load_vector_fd(vdev, "interrupt", i);
        if (fd >= 0) {
            vfio_pci_vector_init(vdev, i);
            vfio_pci_msi_set_handler(vdev, i);
        }

        if (vfio_cpr_load_vector_fd(vdev, "kvm_interrupt", i) >= 0) {
            vfio_pci_add_kvm_msi_virq(vdev, vector, i, msix);
        } else {
            vdev->msi_vectors[i].virq = -1;
        }

        if (msix && msix_is_pending(pdev, i) && msix_is_masked(pdev, i)) {
            set_bit(i, vdev->msix->pending);
            pending = true;
        }
    }

    vfio_pci_commit_kvm_msi_virq_batch(vdev);

    if (msix) {
        memory_region_set_enabled(&pdev->msix_pba_mmio, pending);
    }
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

static int vfio_cpr_pci_post_load(void *opaque, int version_id)
{
    VFIOPCIDevice *vdev = opaque;
    PCIDevice *pdev = &vdev->pdev;
    int nr_vectors;

    vfio_sub_page_bar_update_mappings(vdev);

    if (msix_enabled(pdev)) {
        vfio_pci_msix_set_notifiers(vdev);
        nr_vectors = vdev->msix->entries;
        vfio_cpr_claim_vectors(vdev, nr_vectors, true);

    } else if (msi_enabled(pdev)) {
        nr_vectors = msi_nr_vectors_allocated(pdev);
        vfio_cpr_claim_vectors(vdev, nr_vectors, false);

    } else if (vfio_pci_read_config(pdev, PCI_INTERRUPT_PIN, 1)) {
        Error *local_err = NULL;
        if (!vfio_pci_intx_enable(vdev, &local_err)) {
            error_report_err(local_err);
            return -1;
        }
    }

    return 0;
}

static bool pci_msix_present(void *opaque, int version_id)
{
    PCIDevice *pdev = opaque;

    return msix_present(pdev);
}

static const VMStateDescription vfio_intx_vmstate = {
    .name = "vfio-cpr-intx",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(pending, VFIOINTx),
        VMSTATE_UINT32(route.mode, VFIOINTx),
        VMSTATE_INT32(route.irq, VFIOINTx),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_VFIO_INTX(_field, _state) {                         \
    .name       = (stringify(_field)),                              \
    .size       = sizeof(VFIOINTx),                                 \
    .vmsd       = &vfio_intx_vmstate,                               \
    .flags      = VMS_STRUCT,                                       \
    .offset     = vmstate_offset_value(_state, _field, VFIOINTx),   \
}

const VMStateDescription vfio_cpr_pci_vmstate = {
    .name = "vfio-cpr-pci",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = vfio_cpr_pci_pre_load,
    .post_load = vfio_cpr_pci_post_load,
    .needed = cpr_incoming_needed,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pdev, VFIOPCIDevice),
        VMSTATE_MSIX_TEST(pdev, VFIOPCIDevice, pci_msix_present),
        VMSTATE_VFIO_INTX(intx, VFIOPCIDevice),
        VMSTATE_END_OF_LIST()
    }
};

static NotifierWithReturn kvm_close_notifier;

static int vfio_cpr_kvm_close_notifier(NotifierWithReturn *notifier,
                                       MigrationEvent *e,
                                       Error **errp)
{
    if (e->type == MIG_EVENT_PRECOPY_DONE) {
        vfio_kvm_device_close();
    }
    return 0;
}

void vfio_cpr_add_kvm_notifier(void)
{
    if (!kvm_close_notifier.notify) {
        migration_add_notifier_mode(&kvm_close_notifier,
                                    vfio_cpr_kvm_close_notifier,
                                    MIG_MODE_CPR_TRANSFER);
    }
}
