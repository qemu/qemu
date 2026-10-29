/*
 * Generic PCI Express Downstream Port emulation
 *
 * Copyright (C) 2026 Nutanix, Inc.
 *
 * Derived from gen_pcie_root_port.c:
 *   Copyright (C) 2017 Red Hat Inc
 *
 * Derived from xio3130_downstream.c:
 *   Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                       VA Linux Systems Japan K.K.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define TYPE_GEN_PCIE_DOWNSTREAM_PORT            "pcie-downstream-port"
OBJECT_DECLARE_SIMPLE_TYPE(GenPCIEDownstreamPort, GEN_PCIE_DOWNSTREAM_PORT)

struct GenPCIEDownstreamPort {
    PCIESlot parent_obj;
};

#define GEN_PCIE_DOWNSTREAM_PORT_AER_OFFSET      0x100
#define GEN_PCIE_DOWNSTREAM_PORT_ACS_OFFSET \
    (GEN_PCIE_DOWNSTREAM_PORT_AER_OFFSET + PCI_ERR_SIZEOF)
#define GEN_PCIE_DOWNSTREAM_PORT_MSIX_NR_VECTOR  1


static int gen_downstream_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msix_init_exclusive_bar(d, GEN_PCIE_DOWNSTREAM_PORT_MSIX_NR_VECTOR,
                                 0, errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    } else {
        msix_vector_use(d, 0);
    }

    return rc;
}

static void gen_downstream_interrupts_uninit(PCIDevice *d)
{
    msix_uninit_exclusive_bar(d);
}

static void gen_downstream_write_config(PCIDevice *d, uint32_t address,
                                        uint32_t val, int len)
{
    uint16_t slt_ctl, slt_sta;

    pcie_cap_slot_get(d, &slt_ctl, &slt_sta);
    pci_bridge_write_config(d, address, val, len);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_cap_slot_write_config(d, slt_ctl, slt_sta, address, val, len);
    pcie_aer_write_config(d, address, val, len);
}

static void gen_downstream_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);

    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_cap_arifwd_reset(d);
    pci_bridge_reset(qdev);
}

static void gen_downstream_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    PCIESlot *s = PCIE_SLOT(d);
    int rc;

    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = gen_downstream_interrupts_init(d, errp);
    if (rc < 0) {
        goto err_bridge;
    }

    rc = pcie_cap_init(d, 0x90, PCI_EXP_TYPE_DOWNSTREAM, p->port, errp);
    if (rc < 0) {
        goto err_int;
    }
    pcie_cap_flr_init(d);
    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s);
    pcie_cap_arifwd_init(d);

    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        error_setg(errp, "Can't add chassis slot, error %d", rc);
        goto err_cap;
    }

    rc = pcie_aer_init(d, PCI_ERR_VER, GEN_PCIE_DOWNSTREAM_PORT_AER_OFFSET,
                       PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err_chassis;
    }

    pcie_acs_init(d, GEN_PCIE_DOWNSTREAM_PORT_ACS_OFFSET);

    return;

err_chassis:
    pcie_chassis_del_slot(s);
err_cap:
    pcie_cap_exit(d);
err_int:
    gen_downstream_interrupts_uninit(d);
err_bridge:
    pci_bridge_exitfn(d);
}

static void gen_downstream_exitfn(PCIDevice *d)
{
    PCIESlot *s = PCIE_SLOT(d);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    gen_downstream_interrupts_uninit(d);
    pci_bridge_exitfn(d);
}

static const VMStateDescription vmstate_gen_pcie_downstream_port = {
    .name = "pcie-downstream-port",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_MSIX(parent_obj.parent_obj.parent_obj.parent_obj,
                     GenPCIEDownstreamPort),
        VMSTATE_END_OF_LIST()
    }
};

static const Property gen_pcie_downstream_port_props[] = {
    DEFINE_PROP_BIT(COMPAT_PROP_PCP, PCIDevice, cap_present,
                    QEMU_PCIE_SLTCAP_PCP_BITNR, true),
    DEFINE_PROP_PCIE_LINK_SPEED("speed", PCIESlot,
                                speed, PCIE_LINK_SPEED_64),
    DEFINE_PROP_PCIE_LINK_WIDTH("width", PCIESlot,
                                width, PCIE_LINK_WIDTH_32),
};

static void gen_pcie_downstream_port_class_init(ObjectClass *klass,
                                                const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = gen_downstream_write_config;
    k->realize = gen_downstream_realize;
    k->exit = gen_downstream_exitfn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_DSP;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "PCI Express Downstream Port";
    device_class_set_legacy_reset(dc, gen_downstream_reset);
    dc->vmsd = &vmstate_gen_pcie_downstream_port;
    device_class_set_props(dc, gen_pcie_downstream_port_props);
}

static const TypeInfo gen_pcie_downstream_port_info = {
    .name          = TYPE_GEN_PCIE_DOWNSTREAM_PORT,
    .parent        = TYPE_PCIE_SLOT,
    .instance_size = sizeof(GenPCIEDownstreamPort),
    .class_init    = gen_pcie_downstream_port_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void gen_pcie_downstream_port_register_types(void)
{
    type_register_static(&gen_pcie_downstream_port_info);
}

type_init(gen_pcie_downstream_port_register_types)
