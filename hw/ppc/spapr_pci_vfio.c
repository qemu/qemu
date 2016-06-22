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
#include <linux/vfio.h>
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"
#include "hw/pci/msix.h"
#include "hw/vfio/vfio.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

#define TYPE_SPAPR_PCI_VFIO_HOST_BRIDGE "spapr-pci-vfio-host-bridge"

#define SPAPR_PCI_VFIO_HOST_BRIDGE(obj) \
    OBJECT_CHECK(sPAPRPHBVFIOState, (obj), TYPE_SPAPR_PCI_VFIO_HOST_BRIDGE)

typedef struct sPAPRPHBVFIOState sPAPRPHBVFIOState;

struct sPAPRPHBVFIOState {
    sPAPRPHBState phb;

    int32_t iommugroupid;
};

static Property spapr_phb_vfio_properties[] = {
    DEFINE_PROP_INT32("iommu", sPAPRPHBVFIOState, iommugroupid, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_phb_vfio_instance_init(Object *obj)
{
    if (!qtest_enabled()) {
        error_report("spapr-pci-vfio-host-bridge is deprecated");
    }
}

bool spapr_phb_eeh_available(sPAPRPHBState *sphb)
{
    return vfio_eeh_as_ok(&sphb->iommu_as);
}

static void spapr_phb_vfio_eeh_reenable(sPAPRPHBState *sphb)
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

int spapr_phb_vfio_eeh_set_option(sPAPRPHBState *sphb,
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

int spapr_phb_vfio_eeh_get_state(sPAPRPHBState *sphb, int *state)
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
       pci_for_each_device(bus, pci_bus_num(bus),
                           spapr_phb_vfio_eeh_clear_dev_msix, NULL);
}

static void spapr_phb_vfio_eeh_pre_reset(sPAPRPHBState *sphb)
{
       PCIHostState *phb = PCI_HOST_BRIDGE(sphb);

       pci_for_each_bus(phb->bus, spapr_phb_vfio_eeh_clear_bus_msix, NULL);
}

int spapr_phb_vfio_eeh_reset(sPAPRPHBState *sphb, int option)
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

int spapr_phb_vfio_eeh_configure(sPAPRPHBState *sphb)
{
    int ret;

    ret = vfio_eeh_as_op(&sphb->iommu_as, VFIO_EEH_PE_CONFIGURE);
    if (ret < 0) {
        return RTAS_OUT_PARAM_ERROR;
    }

    return RTAS_OUT_SUCCESS;
}

static void spapr_phb_vfio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = spapr_phb_vfio_properties;
}

static const TypeInfo spapr_phb_vfio_info = {
    .name          = TYPE_SPAPR_PCI_VFIO_HOST_BRIDGE,
    .parent        = TYPE_SPAPR_PCI_HOST_BRIDGE,
    .instance_size = sizeof(sPAPRPHBVFIOState),
    .instance_init = spapr_phb_vfio_instance_init,
    .class_init    = spapr_phb_vfio_class_init,
};

static void spapr_pci_vfio_register_types(void)
{
    type_register_static(&spapr_phb_vfio_info);
}

type_init(spapr_pci_vfio_register_types)
