/*
 * Base class for PCI Express Root Ports
 *
 * Copyright (C) 2017 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * Most of the code was migrated from hw/pci-bridge/ioh3420.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pcie_port.h"

static void rp_aer_vector_update(PCIDevice *d)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);

    if (rpc->aer_vector) {
        pcie_aer_root_set_vector(d, rpc->aer_vector(d));
    }
}

static void rp_write_config(PCIDevice *d, uint32_t address,
                            uint32_t val, int len)
{
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);

    pci_bridge_write_config(d, address, val, len);
    rp_aer_vector_update(d);
    pcie_cap_slot_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);
}

static void rp_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);

    rp_aer_vector_update(d);
    pcie_cap_root_reset(d);
    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_cap_arifwd_reset(d);
    pcie_aer_root_reset(d);
    pci_bridge_reset(qdev);
    pci_bridge_disable_base_limit(d);
}

static void rp_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    PCIESlot *s = PCIE_SLOT(d);
    PCIDeviceClass *dc = PCI_DEVICE_GET_CLASS(d);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);
    int rc;

    pci_config_set_interrupt_pin(d->config, 1);
    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = pci_bridge_ssvid_init(d, rpc->ssvid_offset, dc->vendor_id,
                               rpc->ssid, errp);
    if (rc < 0) {
        error_append_hint(errp, "Can't init SSV ID, error %d\n", rc);
        goto err_bridge;
    }

    if (rpc->interrupts_init) {
        rc = rpc->interrupts_init(d, errp);
        if (rc < 0) {
            goto err_bridge;
        }
    }

    rc = pcie_cap_init(d, rpc->exp_offset, PCI_EXP_TYPE_ROOT_PORT,
                       p->port, errp);
    if (rc < 0) {
        error_append_hint(errp, "Can't add Root Port capability, "
                          "error %d\n", rc);
        goto err_int;
    }

    pcie_cap_arifwd_init(d);
    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s->slot);
    pcie_cap_root_init(d);

    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        error_setg(errp, "Can't add chassis slot, error %d", rc);
        goto err_pcie_cap;
    }

    rc = pcie_aer_init(d, PCI_ERR_VER, rpc->aer_offset,
                       PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err;
    }
    pcie_aer_root_init(d);
    rp_aer_vector_update(d);

    return;

err:
    pcie_chassis_del_slot(s);
err_pcie_cap:
    pcie_cap_exit(d);
err_int:
    if (rpc->interrupts_uninit) {
        rpc->interrupts_uninit(d);
    }
err_bridge:
    pci_bridge_exitfn(d);
}

static void rp_exit(PCIDevice *d)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);
    PCIESlot *s = PCIE_SLOT(d);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    if (rpc->interrupts_uninit) {
        rpc->interrupts_uninit(d);
    }
    pci_bridge_exitfn(d);
}

static Property rp_props[] = {
    DEFINE_PROP_BIT(COMPAT_PROP_PCP, PCIDevice, cap_present,
                    QEMU_PCIE_SLTCAP_PCP_BITNR, true),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->is_express = 1;
    k->is_bridge = 1;
    k->config_write = rp_write_config;
    k->realize = rp_realize;
    k->exit = rp_exit;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->reset = rp_reset;
    dc->props = rp_props;
}

static const TypeInfo rp_info = {
    .name          = TYPE_PCIE_ROOT_PORT,
    .parent        = TYPE_PCIE_SLOT,
    .class_init    = rp_class_init,
    .abstract      = true,
    .class_size = sizeof(PCIERootPortClass),
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
