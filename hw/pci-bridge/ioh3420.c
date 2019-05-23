/*
 * ioh3420.c
 * Intel X58 north bridge IOH
 * PCI Express root port device id 3420
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "qemu/module.h"

#define PCI_DEVICE_ID_IOH_EPORT         0x3420  /* D0:F0 express mode */
#define PCI_DEVICE_ID_IOH_REV           0x2
#define IOH_EP_SSVID_OFFSET             0x40
#define IOH_EP_SSVID_SVID               PCI_VENDOR_ID_INTEL
#define IOH_EP_SSVID_SSID               0
#define IOH_EP_MSI_OFFSET               0x60
#define IOH_EP_MSI_SUPPORTED_FLAGS      PCI_MSI_FLAGS_MASKBIT
#define IOH_EP_MSI_NR_VECTOR            2
#define IOH_EP_EXP_OFFSET               0x90
#define IOH_EP_AER_OFFSET               0x100

/*
 * If two MSI vector are allocated, Advanced Error Interrupt Message Number
 * is 1. otherwise 0.
 * 17.12.5.10 RPERRSTS,  32:27 bit Advanced Error Interrupt Message Number.
 */
static uint8_t ioh3420_aer_vector(const PCIDevice *d)
{
    switch (msi_nr_vectors_allocated(d)) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
    case 8:
    case 16:
    case 32:
    default:
        break;
    }
    abort();
    return 0;
}

static int ioh3420_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msi_init(d, IOH_EP_MSI_OFFSET, IOH_EP_MSI_NR_VECTOR,
                  IOH_EP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  IOH_EP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT,
                  errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    }

    return rc;
}

static void ioh3420_interrupts_uninit(PCIDevice *d)
{
    msi_uninit(d);
}

static const VMStateDescription vmstate_ioh3420 = {
    .name = "ioh-3240-express-root-port",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static void ioh3420_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_IOH_EPORT;
    k->revision = PCI_DEVICE_ID_IOH_REV;
    dc->desc = "Intel IOH device id 3420 PCIE Root Port";
    dc->vmsd = &vmstate_ioh3420;
    rpc->aer_vector = ioh3420_aer_vector;
    rpc->interrupts_init = ioh3420_interrupts_init;
    rpc->interrupts_uninit = ioh3420_interrupts_uninit;
    rpc->exp_offset = IOH_EP_EXP_OFFSET;
    rpc->aer_offset = IOH_EP_AER_OFFSET;
    rpc->ssvid_offset = IOH_EP_SSVID_OFFSET;
    rpc->ssid = IOH_EP_SSVID_SSID;
}

static const TypeInfo ioh3420_info = {
    .name          = "ioh3420",
    .parent        = TYPE_PCIE_ROOT_PORT,
    .class_init    = ioh3420_class_init,
};

static void ioh3420_register_types(void)
{
    type_register_static(&ioh3420_info);
}

type_init(ioh3420_register_types)
