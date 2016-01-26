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
#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"
#include "hw/pci/msix.h"
#include "linux/vfio.h"
#include "hw/vfio/vfio.h"

static Property spapr_phb_vfio_properties[] = {
    DEFINE_PROP_INT32("iommu", sPAPRPHBVFIOState, iommugroupid, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_phb_vfio_finish_realize(sPAPRPHBState *sphb, Error **errp)
{
    sPAPRPHBVFIOState *svphb = SPAPR_PCI_VFIO_HOST_BRIDGE(sphb);
    struct vfio_iommu_spapr_tce_info info = { .argsz = sizeof(info) };
    int ret;
    sPAPRTCETable *tcet;
    uint32_t liobn = svphb->phb.dma_liobn;

    if (svphb->iommugroupid == -1) {
        error_setg(errp, "Wrong IOMMU group ID %d", svphb->iommugroupid);
        return;
    }

    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_CHECK_EXTENSION,
                               (void *) VFIO_SPAPR_TCE_IOMMU);
    if (ret != 1) {
        error_setg_errno(errp, -ret,
                         "spapr-vfio: SPAPR extension is not supported");
        return;
    }

    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_IOMMU_SPAPR_TCE_GET_INFO, &info);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "spapr-vfio: get info from container failed");
        return;
    }

    tcet = spapr_tce_new_table(DEVICE(sphb), liobn, info.dma32_window_start,
                               SPAPR_TCE_PAGE_SHIFT,
                               info.dma32_window_size >> SPAPR_TCE_PAGE_SHIFT,
                               true);
    if (!tcet) {
        error_setg(errp, "spapr-vfio: failed to create VFIO TCE table");
        return;
    }

    /* Register default 32bit DMA window */
    memory_region_add_subregion(&sphb->iommu_root, tcet->bus_offset,
                                spapr_tce_get_iommu(tcet));
}

static void spapr_phb_vfio_eeh_reenable(sPAPRPHBVFIOState *svphb)
{
    struct vfio_eeh_pe_op op = {
        .argsz = sizeof(op),
        .op    = VFIO_EEH_PE_ENABLE
    };

    vfio_container_ioctl(&svphb->phb.iommu_as,
                         svphb->iommugroupid, VFIO_EEH_PE_OP, &op);
}

static void spapr_phb_vfio_reset(DeviceState *qdev)
{
    /*
     * The PE might be in frozen state. To reenable the EEH
     * functionality on it will clean the frozen state, which
     * ensures that the contained PCI devices will work properly
     * after reboot.
     */
    spapr_phb_vfio_eeh_reenable(SPAPR_PCI_VFIO_HOST_BRIDGE(qdev));
}

static int spapr_phb_vfio_eeh_set_option(sPAPRPHBState *sphb,
                                         unsigned int addr, int option)
{
    sPAPRPHBVFIOState *svphb = SPAPR_PCI_VFIO_HOST_BRIDGE(sphb);
    struct vfio_eeh_pe_op op = { .argsz = sizeof(op) };
    int ret;

    switch (option) {
    case RTAS_EEH_DISABLE:
        op.op = VFIO_EEH_PE_DISABLE;
        break;
    case RTAS_EEH_ENABLE: {
        PCIHostState *phb;
        PCIDevice *pdev;

        /*
         * The EEH functionality is enabled on basis of PCI device,
         * instead of PE. We need check the validity of the PCI
         * device address.
         */
        phb = PCI_HOST_BRIDGE(sphb);
        pdev = pci_find_device(phb->bus,
                               (addr >> 16) & 0xFF, (addr >> 8) & 0xFF);
        if (!pdev || !object_dynamic_cast(OBJECT(pdev), "vfio-pci")) {
            return RTAS_OUT_PARAM_ERROR;
        }

        op.op = VFIO_EEH_PE_ENABLE;
        break;
    }
    case RTAS_EEH_THAW_IO:
        op.op = VFIO_EEH_PE_UNFREEZE_IO;
        break;
    case RTAS_EEH_THAW_DMA:
        op.op = VFIO_EEH_PE_UNFREEZE_DMA;
        break;
    default:
        return RTAS_OUT_PARAM_ERROR;
    }

    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_EEH_PE_OP, &op);
    if (ret < 0) {
        return RTAS_OUT_HW_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

static int spapr_phb_vfio_eeh_get_state(sPAPRPHBState *sphb, int *state)
{
    sPAPRPHBVFIOState *svphb = SPAPR_PCI_VFIO_HOST_BRIDGE(sphb);
    struct vfio_eeh_pe_op op = { .argsz = sizeof(op) };
    int ret;

    op.op = VFIO_EEH_PE_GET_STATE;
    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_EEH_PE_OP, &op);
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
       pci_for_each_device(bus, pci_bus_num(bus),
                           spapr_phb_vfio_eeh_clear_dev_msix, NULL);
}

static void spapr_phb_vfio_eeh_pre_reset(sPAPRPHBState *sphb)
{
       PCIHostState *phb = PCI_HOST_BRIDGE(sphb);

       pci_for_each_bus(phb->bus, spapr_phb_vfio_eeh_clear_bus_msix, NULL);
}

static int spapr_phb_vfio_eeh_reset(sPAPRPHBState *sphb, int option)
{
    sPAPRPHBVFIOState *svphb = SPAPR_PCI_VFIO_HOST_BRIDGE(sphb);
    struct vfio_eeh_pe_op op = { .argsz = sizeof(op) };
    int ret;

    switch (option) {
    case RTAS_SLOT_RESET_DEACTIVATE:
        op.op = VFIO_EEH_PE_RESET_DEACTIVATE;
        break;
    case RTAS_SLOT_RESET_HOT:
        spapr_phb_vfio_eeh_pre_reset(sphb);
        op.op = VFIO_EEH_PE_RESET_HOT;
        break;
    case RTAS_SLOT_RESET_FUNDAMENTAL:
        spapr_phb_vfio_eeh_pre_reset(sphb);
        op.op = VFIO_EEH_PE_RESET_FUNDAMENTAL;
        break;
    default:
        return RTAS_OUT_PARAM_ERROR;
    }

    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_EEH_PE_OP, &op);
    if (ret < 0) {
        return RTAS_OUT_HW_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

static int spapr_phb_vfio_eeh_configure(sPAPRPHBState *sphb)
{
    sPAPRPHBVFIOState *svphb = SPAPR_PCI_VFIO_HOST_BRIDGE(sphb);
    struct vfio_eeh_pe_op op = { .argsz = sizeof(op) };
    int ret;

    op.op = VFIO_EEH_PE_CONFIGURE;
    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_EEH_PE_OP, &op);
    if (ret < 0) {
        return RTAS_OUT_PARAM_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

static void spapr_phb_vfio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    sPAPRPHBClass *spc = SPAPR_PCI_HOST_BRIDGE_CLASS(klass);

    dc->props = spapr_phb_vfio_properties;
    dc->reset = spapr_phb_vfio_reset;
    spc->finish_realize = spapr_phb_vfio_finish_realize;
    spc->eeh_set_option = spapr_phb_vfio_eeh_set_option;
    spc->eeh_get_state = spapr_phb_vfio_eeh_get_state;
    spc->eeh_reset = spapr_phb_vfio_eeh_reset;
    spc->eeh_configure = spapr_phb_vfio_eeh_configure;
}

static const TypeInfo spapr_phb_vfio_info = {
    .name          = TYPE_SPAPR_PCI_VFIO_HOST_BRIDGE,
    .parent        = TYPE_SPAPR_PCI_HOST_BRIDGE,
    .instance_size = sizeof(sPAPRPHBVFIOState),
    .class_init    = spapr_phb_vfio_class_init,
    .class_size    = sizeof(sPAPRPHBClass),
};

static void spapr_pci_vfio_register_types(void)
{
    type_register_static(&spapr_phb_vfio_info);
}

type_init(spapr_pci_vfio_register_types)
