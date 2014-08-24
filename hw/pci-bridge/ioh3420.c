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

#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "ioh3420.h"

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

static void ioh3420_aer_vector_update(PCIDevice *d)
{
    pcie_aer_root_set_vector(d, ioh3420_aer_vector(d));
}

static void ioh3420_write_config(PCIDevice *d,
                                   uint32_t address, uint32_t val, int len)
{
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);

    pci_bridge_write_config(d, address, val, len);
    ioh3420_aer_vector_update(d);
    pcie_cap_slot_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);
}

static void ioh3420_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);

    ioh3420_aer_vector_update(d);
    pcie_cap_root_reset(d);
    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_aer_root_reset(d);
    pci_bridge_reset(qdev);
    pci_bridge_disable_base_limit(d);
}

static int ioh3420_initfn(PCIDevice *d)
{
    PCIEPort *p = PCIE_PORT(d);
    PCIESlot *s = PCIE_SLOT(d);
    int rc;

    rc = pci_bridge_initfn(d, TYPE_PCIE_BUS);
    if (rc < 0) {
        return rc;
    }

    pcie_port_init_reg(d);

    rc = pci_bridge_ssvid_init(d, IOH_EP_SSVID_OFFSET,
                               IOH_EP_SSVID_SVID, IOH_EP_SSVID_SSID);
    if (rc < 0) {
        goto err_bridge;
    }
    rc = msi_init(d, IOH_EP_MSI_OFFSET, IOH_EP_MSI_NR_VECTOR,
                  IOH_EP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  IOH_EP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT);
    if (rc < 0) {
        goto err_bridge;
    }
    rc = pcie_cap_init(d, IOH_EP_EXP_OFFSET, PCI_EXP_TYPE_ROOT_PORT, p->port);
    if (rc < 0) {
        goto err_msi;
    }

    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s->slot);
    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        goto err_pcie_cap;
    }
    pcie_cap_root_init(d);
    rc = pcie_aer_init(d, IOH_EP_AER_OFFSET);
    if (rc < 0) {
        goto err;
    }
    pcie_aer_root_init(d);
    ioh3420_aer_vector_update(d);
    return 0;

err:
    pcie_chassis_del_slot(s);
err_pcie_cap:
    pcie_cap_exit(d);
err_msi:
    msi_uninit(d);
err_bridge:
    pci_bridge_exitfn(d);
    return rc;
}

static void ioh3420_exitfn(PCIDevice *d)
{
    PCIESlot *s = PCIE_SLOT(d);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    msi_uninit(d);
    pci_bridge_exitfn(d);
}

static Property ioh3420_props[] = {
    DEFINE_PROP_BIT(COMPAT_PROP_PCP, PCIDevice, cap_present,
                    QEMU_PCIE_SLTCAP_PCP_BITNR, true),
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription vmstate_ioh3420 = {
    .name = "ioh-3240-express-root-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCIE_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static void ioh3420_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->is_express = 1;
    k->is_bridge = 1;
    k->config_write = ioh3420_write_config;
    k->init = ioh3420_initfn;
    k->exit = ioh3420_exitfn;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_IOH_EPORT;
    k->revision = PCI_DEVICE_ID_IOH_REV;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Intel IOH device id 3420 PCIE Root Port";
    dc->reset = ioh3420_reset;
    dc->vmsd = &vmstate_ioh3420;
    dc->props = ioh3420_props;
}

static const TypeInfo ioh3420_info = {
    .name          = "ioh3420",
    .parent        = TYPE_PCIE_SLOT,
    .class_init    = ioh3420_class_init,
};

static void ioh3420_register_types(void)
{
    type_register_static(&ioh3420_info);
}

type_init(ioh3420_register_types)

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 *  indent-tab-mode: nil
 * End:
 */
