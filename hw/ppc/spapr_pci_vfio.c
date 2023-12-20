/*
 * QEMU sPAPR PCI host for VFIO
 *
 * Copyright (c) 2011-2014 Alexey Kardashevskiy, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_device.h"
#include "hw/vfio/vfio-common.h"
#include "qemu/error-report.h"
#include CONFIG_DEVICES /* CONFIG_VFIO_PCI */

/*
 * Interfaces for IBM EEH (Enhanced Error Handling)
 */
#ifdef CONFIG_VFIO_PCI
static bool vfio_eeh_container_ok(VFIOContainer *container)
{
    /*
     * As of 2016-03-04 (linux-4.5) the host kernel EEH/VFIO
     * implementation is broken if there are multiple groups in a
     * container.  The hardware works in units of Partitionable
     * Endpoints (== IOMMU groups) and the EEH operations naively
     * iterate across all groups in the container, without any logic
     * to make sure the groups have their state synchronized.  For
     * certain operations (ENABLE) that might be ok, until an error
     * occurs, but for others (GET_STATE) it's clearly broken.
     */

    /*
     * XXX Once fixed kernels exist, test for them here
     */

    if (QLIST_EMPTY(&container->group_list)) {
        return false;
    }

    if (QLIST_NEXT(QLIST_FIRST(&container->group_list), container_next)) {
        return false;
    }

    return true;
}

static int vfio_eeh_container_op(VFIOContainer *container, uint32_t op)
{
    struct vfio_eeh_pe_op pe_op = {
        .argsz = sizeof(pe_op),
        .op = op,
    };
    int ret;

    if (!vfio_eeh_container_ok(container)) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x: "
                     "kernel requires a container with exactly one group", op);
        return -EPERM;
    }

    ret = ioctl(container->fd, VFIO_EEH_PE_OP, &pe_op);
    if (ret < 0) {
        error_report("vfio/eeh: EEH_PE_OP 0x%x failed: %m", op);
        return -errno;
    }

    return ret;
}

static VFIOContainer *vfio_eeh_as_container(AddressSpace *as)
{
    VFIOAddressSpace *space = vfio_get_address_space(as);
    VFIOContainerBase *bcontainer = NULL;

    if (QLIST_EMPTY(&space->containers)) {
        /* No containers to act on */
        goto out;
    }

    bcontainer = QLIST_FIRST(&space->containers);

    if (QLIST_NEXT(bcontainer, next)) {
        /*
         * We don't yet have logic to synchronize EEH state across
         * multiple containers
         */
        bcontainer = NULL;
        goto out;
    }

out:
    vfio_put_address_space(space);
    return container_of(bcontainer, VFIOContainer, bcontainer);
}

static bool vfio_eeh_as_ok(AddressSpace *as)
{
    VFIOContainer *container = vfio_eeh_as_container(as);

    return (container != NULL) && vfio_eeh_container_ok(container);
}

static int vfio_eeh_as_op(AddressSpace *as, uint32_t op)
{
    VFIOContainer *container = vfio_eeh_as_container(as);

    if (!container) {
        return -ENODEV;
    }
    return vfio_eeh_container_op(container, op);
}

bool spapr_phb_eeh_available(SpaprPhbState *sphb)
{
    return vfio_eeh_as_ok(&sphb->iommu_as);
}

static void spapr_phb_vfio_eeh_reenable(SpaprPhbState *sphb)
{
    vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_ENABLE);
}

void spapr_phb_vfio_reset(DeviceState *qdev)
{
    /*
     * The PE might be in frozen state. To reenable the EEH
     * functionality on it will clean the frozen state, which
     * ensures that the contained PCI devices will work properly
     * after reboot.
     */
    spapr_phb_vfio_eeh_reenable(SPAPR_PCI_HOST_BRIDGE(qdev));
}

static void spapr_eeh_pci_find_device(PCIBus *bus, PCIDevice *pdev,
                                      void *opaque)
{
    bool *found = opaque;

    if (object_dynamic_cast(OBJECT(pdev), "vfio-pci")) {
        *found = true;
    }
}

int spapr_phb_vfio_eeh_set_option(SpaprPhbState *sphb,
                                  unsigned int addr, int option)
{
    uint32_t op;
    int ret;

    switch (option) {
    case RTAS_EEH_DISABLE:
        op = VFIO_EEH_PE_DISABLE;
        break;
    case RTAS_EEH_ENABLE: {
        PCIHostState *phb;
        bool found = false;

        /*
         * The EEH functionality is enabled per sphb level instead of
         * per PCI device. We have already identified this specific sphb
         * based on buid passed as argument to ibm,set-eeh-option rtas
         * call. Now we just need to check the validity of the PCI
         * pass-through devices (vfio-pci) under this sphb bus.
         * We have already validated that all the devices under this sphb
         * are from same iommu group (within same PE) before coming here.
         *
         * Prior to linux commit 98ba956f6a389 ("powerpc/pseries/eeh:
         * Rework device EEH PE determination") kernel would call
         * eeh-set-option for each device in the PE using the device's
         * config_address as the argument rather than the PE address.
         * Hence if we check validity of supplied config_addr whether
         * it matches to this PHB will cause issues with older kernel
         * versions v5.9 and older. If we return an error from
         * eeh-set-option when the argument isn't a valid PE address
         * then older kernels (v5.9 and older) will interpret that as
         * EEH not being supported.
         */
        phb = PCI_HOST_BRIDGE(sphb);
        pci_for_each_device(phb->bus, (addr >> 16) & 0xFF,
                            spapr_eeh_pci_find_device, &found);

        if (!found) {
            return RTAS_OUT_PARAM_ERROR;
        }

        op = VFIO_EEH_PE_ENABLE;
        break;
    }
    case RTAS_EEH_THAW_IO:
        op = VFIO_EEH_PE_UNFREEZE_IO;
        break;
    case RTAS_EEH_THAW_DMA:
        op = VFIO_EEH_PE_UNFREEZE_DMA;
        break;
    default:
        return RTAS_OUT_PARAM_ERROR;
    }

    ret = vfio_eeh_as_op(&sphb->iommu_as, op);
    if (ret < 0) {
        return RTAS_OUT_HW_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

int spapr_phb_vfio_eeh_get_state(SpaprPhbState *sphb, int *state)
{
    int ret;

    ret = vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_GET_STATE);
    if (ret < 0) {
        return RTAS_OUT_PARAM_ERROR;
    }

    *state = ret;
    return RTAS_OUT_SUCCESS;
}

static void spapr_phb_vfio_eeh_clear_dev_msix(PCIBus *bus,
                                              PCIDevice *pdev,
                                              void *opaque)
{
    /* Check if the device is VFIO PCI device */
    if (!object_dynamic_cast(OBJECT(pdev), "vfio-pci")) {
        return;
    }

    /*
     * The MSIx table will be cleaned out by reset. We need
     * disable it so that it can be reenabled properly. Also,
     * the cached MSIx table should be cleared as it's not
     * reflecting the contents in hardware.
     */
    if (msix_enabled(pdev)) {
        uint16_t flags;

        flags = pci_host_config_read_common(pdev,
                                            pdev->msix_cap + PCI_MSIX_FLAGS,
                                            pci_config_size(pdev), 2);
        flags &= ~PCI_MSIX_FLAGS_ENABLE;
        pci_host_config_write_common(pdev,
                                     pdev->msix_cap + PCI_MSIX_FLAGS,
                                     pci_config_size(pdev), flags, 2);
    }

    msix_reset(pdev);
}

static void spapr_phb_vfio_eeh_clear_bus_msix(PCIBus *bus, void *opaque)
{
       pci_for_each_device_under_bus(bus, spapr_phb_vfio_eeh_clear_dev_msix,
                                     NULL);
}

static void spapr_phb_vfio_eeh_pre_reset(SpaprPhbState *sphb)
{
       PCIHostState *phb = PCI_HOST_BRIDGE(sphb);

       pci_for_each_bus(phb->bus, spapr_phb_vfio_eeh_clear_bus_msix, NULL);
}

int spapr_phb_vfio_eeh_reset(SpaprPhbState *sphb, int option)
{
    uint32_t op;
    int ret;

    switch (option) {
    case RTAS_SLOT_RESET_DEACTIVATE:
        op = VFIO_EEH_PE_RESET_DEACTIVATE;
        break;
    case RTAS_SLOT_RESET_HOT:
        spapr_phb_vfio_eeh_pre_reset(sphb);
        op = VFIO_EEH_PE_RESET_HOT;
        break;
    case RTAS_SLOT_RESET_FUNDAMENTAL:
        spapr_phb_vfio_eeh_pre_reset(sphb);
        op = VFIO_EEH_PE_RESET_FUNDAMENTAL;
        break;
    default:
        return RTAS_OUT_PARAM_ERROR;
    }

    ret = vfio_eeh_as_op(&sphb->iommu_as, op);
    if (ret < 0) {
        return RTAS_OUT_HW_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

int spapr_phb_vfio_eeh_configure(SpaprPhbState *sphb)
{
    int ret;

    ret = vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_CONFIGURE);
    if (ret < 0) {
        return RTAS_OUT_PARAM_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

#else

bool spapr_phb_eeh_available(SpaprPhbState *sphb)
{
    return false;
}

void spapr_phb_vfio_reset(DeviceState *qdev)
{
}

int spapr_phb_vfio_eeh_set_option(SpaprPhbState *sphb,
                                  unsigned int addr, int option)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_eeh_get_state(SpaprPhbState *sphb, int *state)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_eeh_reset(SpaprPhbState *sphb, int option)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_eeh_configure(SpaprPhbState *sphb)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

#endif /* CONFIG_VFIO_PCI */
