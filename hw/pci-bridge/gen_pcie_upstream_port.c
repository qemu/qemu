/*
 * Generic PCI Express Upstream Port emulation
 *
 * Copyright (C) 2026 Nutanix, Inc.
 *
 * Derived from gen_pcie_root_port.c:
 *   Copyright (C) 2017 Red Hat Inc
 *
 * Derived from xio3130_upstream.c:
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

#define TYPE_GEN_PCIE_UPSTREAM_PORT              "pcie-upstream-port"
OBJECT_DECLARE_SIMPLE_TYPE(GenPCIEUpstreamPort, GEN_PCIE_UPSTREAM_PORT)

struct GenPCIEUpstreamPort {
    PCIEPort parent_obj;

    PCIExpLinkSpeed speed;
    PCIExpLinkWidth width;
};

#define GEN_PCIE_UPSTREAM_PORT_AER_OFFSET        0x100
#define GEN_PCIE_UPSTREAM_PORT_MSIX_NR_VECTOR    1

static int gen_upstream_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msix_init_exclusive_bar(d, GEN_PCIE_UPSTREAM_PORT_MSIX_NR_VECTOR,
                                 0, errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    } else {
        msix_vector_use(d, 0);
    }

    return rc;
}

static void gen_upstream_interrupts_uninit(PCIDevice *d)
{
    msix_uninit_exclusive_bar(d);
}

static void gen_upstream_write_config(PCIDevice *d, uint32_t address,
                                      uint32_t val, int len)
{
    pci_bridge_write_config(d, address, val, len);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
}

static void gen_upstream_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);
    GenPCIEUpstreamPort *usp = GEN_PCIE_UPSTREAM_PORT(d);

    pci_bridge_reset(qdev);
    pcie_cap_deverr_reset(d);
    pcie_cap_fill_link_ep_usp(d, usp->width, usp->speed, false);
}

static void gen_upstream_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    int rc;

    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = gen_upstream_interrupts_init(d, errp);
    if (rc < 0) {
        goto err_bridge;
    }

    rc = pcie_cap_init(d, 0x90, PCI_EXP_TYPE_UPSTREAM, p->port, errp);
    if (rc < 0) {
        goto err_int;
    }
    pcie_cap_flr_init(d);
    pcie_cap_deverr_init(d);

    rc = pcie_aer_init(d, PCI_ERR_VER, GEN_PCIE_UPSTREAM_PORT_AER_OFFSET,
                       PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err_cap;
    }

    return;

err_cap:
    pcie_cap_exit(d);
err_int:
    gen_upstream_interrupts_uninit(d);
err_bridge:
    pci_bridge_exitfn(d);
}

static void gen_upstream_exitfn(PCIDevice *d)
{
    pcie_aer_exit(d);
    pcie_cap_exit(d);
    gen_upstream_interrupts_uninit(d);
    pci_bridge_exitfn(d);
}

static const VMStateDescription vmstate_gen_pcie_upstream_port = {
    .name = "pcie-upstream-port",
    .priority = MIG_PRI_PCI_BUS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj, PCIEPort),
        VMSTATE_STRUCT(parent_obj.parent_obj.exp.aer_log, PCIEPort, 0,
                       vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_MSIX(parent_obj.parent_obj.parent_obj, GenPCIEUpstreamPort),
        VMSTATE_END_OF_LIST()
    }
};

static const Property gen_pcie_upstream_port_props[] = {
    DEFINE_PROP_PCIE_LINK_SPEED("speed", GenPCIEUpstreamPort,
                                speed, PCIE_LINK_SPEED_64),
    DEFINE_PROP_PCIE_LINK_WIDTH("width", GenPCIEUpstreamPort,
                                width, PCIE_LINK_WIDTH_32),
};

static void gen_pcie_upstream_port_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = gen_upstream_write_config;
    k->realize = gen_upstream_realize;
    k->exit = gen_upstream_exitfn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_USP;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "PCI Express Upstream Port";
    device_class_set_legacy_reset(dc, gen_upstream_reset);
    dc->vmsd = &vmstate_gen_pcie_upstream_port;
    device_class_set_props(dc, gen_pcie_upstream_port_props);
}

static const TypeInfo gen_pcie_upstream_port_info = {
    .name          = TYPE_GEN_PCIE_UPSTREAM_PORT,
    .parent        = TYPE_PCIE_PORT,
    .instance_size = sizeof(GenPCIEUpstreamPort),
    .class_init    = gen_pcie_upstream_port_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void gen_pcie_upstream_port_register_types(void)
{
    type_register_static(&gen_pcie_upstream_port_info);
}

type_init(gen_pcie_upstream_port_register_types)
