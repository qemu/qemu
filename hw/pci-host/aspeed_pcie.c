/*
 * ASPEED PCIe Host Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 * Copyright (c) 2022 Cédric Le Goater <clg@kaod.org>
 *
 * Authors:
 *   Cédric Le Goater <clg@kaod.org>
 *   Jamin Lin <jamin_lin@aspeedtech.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Based on previous work from Cédric Le Goater.
 * Modifications extend support for the ASPEED AST2600 and AST2700 platforms.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/pci/pci_host.h"
#include "hw/pci-host/aspeed_pcie.h"
#include "hw/pci/msi.h"
#include "trace.h"

/*
 * PCIe PHY
 *
 * PCIe Host Controller (PCIEH)
 */

/* AST2600 */
REG32(PEHR_ID,     0x00)
    FIELD(PEHR_ID, DEV, 16, 16)
REG32(PEHR_CLASS_CODE,  0x04)
REG32(PEHR_DATALINK,    0x10)
REG32(PEHR_PROTECT,     0x7C)
    FIELD(PEHR_PROTECT, LOCK, 0, 8)
REG32(PEHR_LINK,        0xC0)
    FIELD(PEHR_LINK, STS, 5, 1)

#define ASPEED_PCIE_PHY_UNLOCK  0xA8

static uint64_t aspeed_pcie_phy_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    AspeedPCIEPhyState *s = ASPEED_PCIE_PHY(opaque);
    uint32_t reg = addr >> 2;
    uint32_t value = 0;

    value = s->regs[reg];

    trace_aspeed_pcie_phy_read(s->id, addr, value);

    return value;
}

static void aspeed_pcie_phy_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned int size)
{
    AspeedPCIEPhyState *s = ASPEED_PCIE_PHY(opaque);
    uint32_t reg = addr >> 2;

    trace_aspeed_pcie_phy_write(s->id, addr, data);

    switch (reg) {
    case R_PEHR_PROTECT:
        data &= R_PEHR_PROTECT_LOCK_MASK;
        s->regs[reg] = !!(data == ASPEED_PCIE_PHY_UNLOCK);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_pcie_phy_ops = {
    .read = aspeed_pcie_phy_read,
    .write = aspeed_pcie_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_pcie_phy_reset(DeviceState *dev)
{
    AspeedPCIEPhyState *s = ASPEED_PCIE_PHY(dev);
    AspeedPCIEPhyClass *apc = ASPEED_PCIE_PHY_GET_CLASS(s);

    memset(s->regs, 0, apc->nr_regs << 2);

    s->regs[R_PEHR_ID] =
        (0x1150 << R_PEHR_ID_DEV_SHIFT) | PCI_VENDOR_ID_ASPEED;
    s->regs[R_PEHR_CLASS_CODE] = 0x06040006;
    s->regs[R_PEHR_DATALINK] = 0xD7040022;
    s->regs[R_PEHR_LINK] = R_PEHR_LINK_STS_MASK;
}

static void aspeed_pcie_phy_realize(DeviceState *dev, Error **errp)
{
    AspeedPCIEPhyState *s = ASPEED_PCIE_PHY(dev);
    AspeedPCIEPhyClass *apc = ASPEED_PCIE_PHY_GET_CLASS(s);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    g_autofree char *name = NULL;

    s->regs = g_new(uint32_t, apc->nr_regs);
    name = g_strdup_printf(TYPE_ASPEED_PCIE_PHY ".regs.%d", s->id);
    memory_region_init_io(&s->mmio, OBJECT(s), &aspeed_pcie_phy_ops, s, name,
                          apc->nr_regs << 2);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void aspeed_pcie_phy_unrealize(DeviceState *dev)
{
    AspeedPCIEPhyState *s = ASPEED_PCIE_PHY(dev);

    g_free(s->regs);
    s->regs = NULL;
}

static const Property aspeed_pcie_phy_props[] = {
    DEFINE_PROP_UINT32("id", AspeedPCIEPhyState, id, 0),
};

static void aspeed_pcie_phy_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedPCIEPhyClass *apc = ASPEED_PCIE_PHY_CLASS(klass);

    dc->desc = "ASPEED PCIe Phy";
    dc->realize = aspeed_pcie_phy_realize;
    dc->unrealize = aspeed_pcie_phy_unrealize;
    device_class_set_legacy_reset(dc, aspeed_pcie_phy_reset);
    device_class_set_props(dc, aspeed_pcie_phy_props);

    apc->nr_regs = 0x100 >> 2;
}

static const TypeInfo aspeed_pcie_phy_info = {
    .name       = TYPE_ASPEED_PCIE_PHY,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedPCIEPhyState),
    .class_init = aspeed_pcie_phy_class_init,
    .class_size = sizeof(AspeedPCIEPhyClass),
};

static void aspeed_pcie_register_types(void)
{
    type_register_static(&aspeed_pcie_phy_info);
}

type_init(aspeed_pcie_register_types);

