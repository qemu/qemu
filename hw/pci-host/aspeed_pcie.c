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
#include "hw/pci/pcie_port.h"
#include "hw/pci-host/aspeed_pcie.h"
#include "hw/pci/msi.h"
#include "trace.h"

/*
 * PCIe Root Device
 * This device exists only on AST2600.
 */

static void aspeed_pcie_root_device_class_init(ObjectClass *klass,
                                               const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "ASPEED PCIe Root Device";
    k->vendor_id = PCI_VENDOR_ID_ASPEED;
    k->device_id = 0x2600;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    k->subsystem_vendor_id = k->vendor_id;
    k->subsystem_id = k->device_id;
    k->revision = 0;

    /*
     * PCI-facing part of the host bridge,
     * not usable without the host-facing part
     */
    dc->user_creatable = false;
}

static const TypeInfo aspeed_pcie_root_device_info = {
    .name = TYPE_ASPEED_PCIE_ROOT_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(AspeedPCIERootDeviceState),
    .class_init = aspeed_pcie_root_device_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

/*
 * PCIe Root Port
 */

#define ASPEED_PCIE_ROOT_PORT_MSI_OFFSET        0x50
#define ASPEED_PCIE_ROOT_PORT_MSI_NR_VECTOR     1
#define ASPEED_PCIE_ROOT_PORT_SSVID_OFFSET      0xC0
#define ASPEED_PCIE_ROOT_PORT_EXP_OFFSET        0x80
#define ASPEED_PCIE_ROOT_PORT_AER_OFFSET        0x100

static uint8_t aspeed_pcie_root_port_aer_vector(const PCIDevice *d)
{
    return 0;
}

static int aspeed_pcie_root_port_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msi_init(d, ASPEED_PCIE_ROOT_PORT_MSI_OFFSET,
                  ASPEED_PCIE_ROOT_PORT_MSI_NR_VECTOR,
                  PCI_MSI_FLAGS_MASKBIT & PCI_MSI_FLAGS_64BIT,
                  PCI_MSI_FLAGS_MASKBIT & PCI_MSI_FLAGS_MASKBIT,
                  errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    }

    return rc;
}

static void aspeed_pcie_root_port_interrupts_uninit(PCIDevice *d)
{
    msi_uninit(d);
}

static void aspeed_pcie_root_port_class_init(ObjectClass *klass,
                                             const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    dc->desc = "ASPEED PCIe Root Port";
    k->vendor_id = PCI_VENDOR_ID_ASPEED;
    k->device_id = 0x1150;
    dc->user_creatable = true;

    rpc->aer_vector = aspeed_pcie_root_port_aer_vector;
    rpc->interrupts_init = aspeed_pcie_root_port_interrupts_init;
    rpc->interrupts_uninit = aspeed_pcie_root_port_interrupts_uninit;
    rpc->exp_offset = ASPEED_PCIE_ROOT_PORT_EXP_OFFSET;
    rpc->aer_offset = ASPEED_PCIE_ROOT_PORT_AER_OFFSET;
    rpc->ssvid_offset = ASPEED_PCIE_ROOT_PORT_SSVID_OFFSET;
    rpc->ssid = 0x1150;
}

static const TypeInfo aspeed_pcie_root_port_info = {
    .name = TYPE_ASPEED_PCIE_ROOT_PORT,
    .parent = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(AspeedPCIERootPortState),
    .class_init = aspeed_pcie_root_port_class_init,
};

/*
 * PCIe Root Complex (RC)
 */

#define ASPEED_PCIE_CFG_RC_MAX_MSI 64

static void aspeed_pcie_rc_set_irq(void *opaque, int irq, int level)
{
    AspeedPCIERcState *rc = (AspeedPCIERcState *) opaque;
    AspeedPCIECfgState *cfg =
        container_of(rc, AspeedPCIECfgState, rc);
    bool intx;

    assert(irq < PCI_NUM_PINS);

    if (level) {
        cfg->regs[cfg->rc_regs->int_sts_reg] |= BIT(irq);
    } else {
        cfg->regs[cfg->rc_regs->int_sts_reg] &= ~BIT(irq);
    }

    intx = !!(cfg->regs[cfg->rc_regs->int_sts_reg] &
              cfg->regs[cfg->rc_regs->int_en_reg]);
    trace_aspeed_pcie_rc_intx_set_irq(cfg->id, irq, intx);
    qemu_set_irq(rc->irq, intx);
}

static int aspeed_pcie_rc_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num % PCI_NUM_PINS;
}

static void aspeed_pcie_rc_msi_notify(AspeedPCIERcState *rc, uint64_t data)
{
    AspeedPCIECfgState *cfg =
           container_of(rc, AspeedPCIECfgState, rc);
    uint32_t reg;

    /* Written data is the HW IRQ number */
    assert(data < ASPEED_PCIE_CFG_RC_MAX_MSI);

    reg = (data < 32) ?
            cfg->rc_regs->msi_sts0_reg : cfg->rc_regs->msi_sts1_reg;
    cfg->regs[reg] |= BIT(data % 32);

    trace_aspeed_pcie_rc_msi_set_irq(cfg->id, data, 1);
    qemu_set_irq(rc->irq, 1);
}

static void aspeed_pcie_rc_msi_write(void *opaque, hwaddr addr, uint64_t data,
                                     unsigned int size)
{
    AspeedPCIERcState *rc = ASPEED_PCIE_RC(opaque);
    AspeedPCIECfgState *cfg =
           container_of(rc, AspeedPCIECfgState, rc);

    trace_aspeed_pcie_rc_msi_notify(cfg->id, addr + rc->msi_addr, data);
    aspeed_pcie_rc_msi_notify(rc, data);
}

static const MemoryRegionOps aspeed_pcie_rc_msi_ops = {
    .write = aspeed_pcie_rc_msi_write,
    .read = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static AddressSpace *aspeed_pcie_rc_get_as(PCIBus *bus, void *opaque, int devfn)
{
    AspeedPCIERcState *rc = ASPEED_PCIE_RC(opaque);
    return &rc->iommu_as;
}

static const PCIIOMMUOps aspeed_pcie_rc_iommu_ops = {
    .get_address_space = aspeed_pcie_rc_get_as,
};

static void aspeed_pcie_rc_realize(DeviceState *dev, Error **errp)
{
    PCIExpressHost *pex = PCIE_HOST_BRIDGE(dev);
    AspeedPCIERcState *rc = ASPEED_PCIE_RC(dev);
    AspeedPCIECfgState *cfg =
           container_of(rc, AspeedPCIECfgState, rc);
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    g_autofree char *ioport_window_name = NULL;
    g_autofree char *mmio_window_name = NULL;
    g_autofree char *iommu_root_name = NULL;
    g_autofree char *dram_alias_name = NULL;
    g_autofree char *root_bus_name = NULL;

    /* PCI configuration space */
    pcie_host_mmcfg_init(pex, PCIE_MMCFG_SIZE_MAX);
    sysbus_init_mmio(sbd, &pex->mmio);

    /* MMIO and IO region */
    memory_region_init(&rc->mmio, OBJECT(rc), "mmio", UINT64_MAX);
    memory_region_init(&rc->io, OBJECT(rc), "io", 0x10000);

    mmio_window_name = g_strdup_printf("pcie.%d.mmio_window", cfg->id);
    memory_region_init_io(&rc->mmio_window, OBJECT(rc), &unassigned_io_ops,
                          OBJECT(rc), mmio_window_name, UINT64_MAX);
    ioport_window_name = g_strdup_printf("pcie.%d.ioport_window", cfg->id);
    memory_region_init_io(&rc->io_window, OBJECT(rc), &unassigned_io_ops,
                          OBJECT(rc), ioport_window_name, 0x10000);

    memory_region_add_subregion(&rc->mmio_window, 0, &rc->mmio);
    memory_region_add_subregion(&rc->io_window, 0, &rc->io);
    sysbus_init_mmio(sbd, &rc->mmio_window);
    sysbus_init_mmio(sbd, &rc->io_window);

    sysbus_init_irq(sbd, &rc->irq);
    root_bus_name = g_strdup_printf("pcie.rc%d", cfg->id);
    pci->bus = pci_register_root_bus(dev, root_bus_name,
                                     aspeed_pcie_rc_set_irq,
                                     aspeed_pcie_rc_map_irq, rc, &rc->mmio,
                                     &rc->io, 0, 4, TYPE_PCIE_BUS);
    pci->bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;

   /*
    * PCIe memory view setup
    *
    * Background:
    * - On AST2700, all Root Complexes use the same MSI address. This MSI
    *   address is not normal system RAM - it is a PCI system memory address.
    *   If we map the MSI/MSI-X window into real system memory, a write from
    *   one EP can be seen by all RCs and wrongly trigger interrupts on them.
    *
    * Design:
    * - MSI/MSI-X here is just a placeholder address so RC and EP can talk.
    *   We make a separate MMIO space (iommu_root) for the MSI window so the
    *   writes stay local to each RC.
    *
    * DMA:
    * - EPs still need access to real system memory for DMA. We add a DRAM
    *   alias in the PCI space so DMA works as expected.
    */
    iommu_root_name = g_strdup_printf("pcie.%d.iommu_root", cfg->id);
    memory_region_init(&rc->iommu_root, OBJECT(rc), iommu_root_name,
                       UINT64_MAX);
    address_space_init(&rc->iommu_as, &rc->iommu_root, iommu_root_name);
    /* setup MSI */
    memory_region_init_io(&rc->msi_window, OBJECT(rc),
                          &aspeed_pcie_rc_msi_ops, rc,
                          "msi_window", 4);
    memory_region_add_subregion(&rc->iommu_root, rc->msi_addr,
                                &rc->msi_window);
    /* setup DRAM for DMA */
    assert(rc->dram_mr != NULL);
    dram_alias_name = g_strdup_printf("pcie.%d.dram_alias", cfg->id);
    memory_region_init_alias(&rc->dram_alias, OBJECT(rc), dram_alias_name,
                             rc->dram_mr, 0, memory_region_size(rc->dram_mr));
    memory_region_add_subregion(&rc->iommu_root, rc->dram_base,
                                &rc->dram_alias);
    pci_setup_iommu(pci->bus, &aspeed_pcie_rc_iommu_ops, rc);

    /* setup root device */
    if (rc->has_rd) {
        object_initialize_child(OBJECT(rc), "root_device", &rc->root_device,
                                TYPE_ASPEED_PCIE_ROOT_DEVICE);
        qdev_prop_set_int32(DEVICE(&rc->root_device), "addr",
                            PCI_DEVFN(0, 0));
        qdev_prop_set_bit(DEVICE(&rc->root_device), "multifunction", false);
        if (!qdev_realize(DEVICE(&rc->root_device), BUS(pci->bus), errp)) {
            return;
        }
    }

    /* setup root port */
    qdev_prop_set_int32(DEVICE(&rc->root_port), "addr", rc->rp_addr);
    qdev_prop_set_uint16(DEVICE(&rc->root_port), "chassis", cfg->id);
    if (!qdev_realize(DEVICE(&rc->root_port), BUS(pci->bus), errp)) {
        return;
    }
}

static const char *aspeed_pcie_rc_root_bus_path(PCIHostState *host_bridge,
                                                PCIBus *rootbus)
{
    AspeedPCIERcState *rc = ASPEED_PCIE_RC(host_bridge);
    AspeedPCIECfgState *cfg =
           container_of(rc, AspeedPCIECfgState, rc);

    snprintf(rc->name, sizeof(rc->name), "%04x:%02x", cfg->id, rc->bus_nr);

    return rc->name;
}

static void aspeed_pcie_rc_instance_init(Object *obj)
{
    AspeedPCIERcState *rc = ASPEED_PCIE_RC(obj);
    AspeedPCIERootPortState *root_port = &rc->root_port;

    object_initialize_child(obj, "root_port", root_port,
                            TYPE_ASPEED_PCIE_ROOT_PORT);
}

static const Property aspeed_pcie_rc_props[] = {
    DEFINE_PROP_UINT32("bus-nr", AspeedPCIERcState, bus_nr, 0),
    DEFINE_PROP_BOOL("has-rd", AspeedPCIERcState, has_rd, 0),
    DEFINE_PROP_UINT32("rp-addr", AspeedPCIERcState, rp_addr, 0),
    DEFINE_PROP_UINT32("msi-addr", AspeedPCIERcState, msi_addr, 0),
    DEFINE_PROP_UINT64("dram-base", AspeedPCIERcState, dram_base, 0),
    DEFINE_PROP_LINK("dram", AspeedPCIERcState, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void aspeed_pcie_rc_class_init(ObjectClass *klass, const void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "ASPEED PCIe RC";
    dc->realize = aspeed_pcie_rc_realize;
    dc->fw_name = "pci";
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);

    hc->root_bus_path = aspeed_pcie_rc_root_bus_path;
    device_class_set_props(dc, aspeed_pcie_rc_props);

    msi_nonbroken = true;
}

static const TypeInfo aspeed_pcie_rc_info = {
    .name = TYPE_ASPEED_PCIE_RC,
    .parent = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(AspeedPCIERcState),
    .instance_init = aspeed_pcie_rc_instance_init,
    .class_init = aspeed_pcie_rc_class_init,
};

/*
 * PCIe Config
 *
 * AHB to PCIe Bus Bridge (H2X)
 *
 * On the AST2600:
 * NOTE: rc_l is not supported by this model.
 * - Registers 0x00 - 0x7F are shared by both PCIe0 (rc_l) and PCIe1 (rc_h).
 * - Registers 0x80 - 0xBF are specific to PCIe0.
 * - Registers 0xC0 - 0xFF are specific to PCIe1.
 *
 * On the AST2700:
 * - The register range 0x00 - 0xFF is assigned to a single PCIe configuration.
 * - There are three PCIe Root Complexes (RCs), each with its own dedicated H2X
 *   register set of size 0x100 (covering offsets 0x00 to 0xFF).
 */

/* AST2600 */
REG32(H2X_CTRL,             0x00)
    FIELD(H2X_CTRL, CLEAR_RX, 4, 1)
REG32(H2X_TX_CLEAR,         0x08)
    FIELD(H2X_TX_CLEAR, IDLE, 0, 1)
REG32(H2X_RDATA,            0x0C)
REG32(H2X_TX_DESC0,         0x10)
REG32(H2X_TX_DESC1,         0x14)
REG32(H2X_TX_DESC2,         0x18)
REG32(H2X_TX_DESC3,         0x1C)
REG32(H2X_TX_DATA,          0x20)
REG32(H2X_TX_STS,           0x24)
    FIELD(H2X_TX_STS, IDLE, 31, 1)
    FIELD(H2X_TX_STS, RC_L_TX_COMP, 24, 1)
    FIELD(H2X_TX_STS, RC_H_TX_COMP, 25, 1)
    FIELD(H2X_TX_STS, TRIG, 0, 1)
REG32(H2X_RC_H_CTRL,        0xC0)
REG32(H2X_RC_H_INT_EN,      0xC4)
REG32(H2X_RC_H_INT_STS,     0xC8)
    SHARED_FIELD(H2X_RC_INT_INTDONE, 4, 1)
    SHARED_FIELD(H2X_RC_INT_INTX, 0, 4)
REG32(H2X_RC_H_RDATA,       0xCC)
REG32(H2X_RC_H_MSI_EN0,     0xE0)
REG32(H2X_RC_H_MSI_EN1,     0xE4)
REG32(H2X_RC_H_MSI_STS0,    0xE8)
REG32(H2X_RC_H_MSI_STS1,    0xEC)

/* AST2700 */
REG32(H2X_CFGE_INT_STS,         0x08)
    FIELD(H2X_CFGE_INT_STS, TX_IDEL, 0, 1)
    FIELD(H2X_CFGE_INT_STS, RX_BUSY, 1, 1)
REG32(H2X_CFGI_TLP,         0x20)
    FIELD(H2X_CFGI_TLP, ADDR, 0, 16)
    FIELD(H2X_CFGI_TLP, BEN, 16, 4)
    FIELD(H2X_CFGI_TLP, WR, 20, 1)
REG32(H2X_CFGI_WDATA,       0x24)
REG32(H2X_CFGI_CTRL,        0x28)
    FIELD(H2X_CFGI_CTRL, FIRE, 0, 1)
REG32(H2X_CFGI_RDATA,       0x2C)
REG32(H2X_CFGE_TLP1,        0x30)
REG32(H2X_CFGE_TLPN,        0x34)
REG32(H2X_CFGE_CTRL,        0x38)
    FIELD(H2X_CFGE_CTRL, FIRE, 0, 1)
REG32(H2X_CFGE_RDATA,       0x3C)
REG32(H2X_INT_EN,          0x40)
REG32(H2X_INT_STS,         0x48)
    FIELD(H2X_INT_STS, INTX, 0, 4)
REG32(H2X_MSI_EN0,          0x50)
REG32(H2X_MSI_EN1,          0x54)
REG32(H2X_MSI_STS0,         0x58)
REG32(H2X_MSI_STS1,         0x5C)

#define TLP_FMTTYPE_CFGRD0  0x04 /* Configuration Read  Type 0 */
#define TLP_FMTTYPE_CFGWR0  0x44 /* Configuration Write Type 0 */
#define TLP_FMTTYPE_CFGRD1  0x05 /* Configuration Read  Type 1 */
#define TLP_FMTTYPE_CFGWR1  0x45 /* Configuration Write Type 1 */

#define PCIE_CFG_FMTTYPE_MASK(x) (((x) >> 24) & 0xff)
#define PCIE_CFG_BYTE_EN(x) ((x) & 0xf)

static const AspeedPCIERegMap aspeed_regmap = {
    .rc = {
        .int_en_reg     = R_H2X_RC_H_INT_EN,
        .int_sts_reg    = R_H2X_RC_H_INT_STS,
        .msi_sts0_reg   = R_H2X_RC_H_MSI_STS0,
        .msi_sts1_reg   = R_H2X_RC_H_MSI_STS1,
    },
};

static const AspeedPCIERegMap aspeed_2700_regmap = {
    .rc = {
        .int_en_reg     = R_H2X_INT_EN,
        .int_sts_reg    = R_H2X_INT_STS,
        .msi_sts0_reg   = R_H2X_MSI_STS0,
        .msi_sts1_reg   = R_H2X_MSI_STS1,
    },
};

static uint64_t aspeed_pcie_cfg_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(opaque);
    uint32_t reg = addr >> 2;
    uint32_t value = 0;

    value = s->regs[reg];

    trace_aspeed_pcie_cfg_read(s->id, addr, value);

    return value;
}

static void aspeed_pcie_cfg_translate_write(uint8_t byte_en, uint32_t *addr,
                                            uint64_t *val, int *len)
{
    uint64_t packed_val = 0;
    int first_bit = -1;
    int index = 0;
    int i;

    *len = ctpop8(byte_en);

    if (*len == 0 || *len > 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid byte enable: 0x%x\n",
                      __func__, byte_en);
        return;
    }

    /* Special case: full 4-byte write must be 4-byte aligned */
    if (byte_en == 0x0f) {
        if ((*addr & 0x3) != 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: 4-byte write not 4-byte aligned: addr=0x%x\n",
                          __func__, *addr);
            return;
        }
        *val &= 0xffffffffULL;
        return;
    }

    for (i = 0; i < 4; i++) {
        if (byte_en & (1 << i)) {
            if (first_bit < 0) {
                first_bit = i;
            }
            packed_val |= ((*val >> (i * 8)) & 0xff) << (index * 8);
            index++;
        }
    }

    *addr += first_bit;
    *val = packed_val;
}

static void aspeed_pcie_cfg_readwrite(AspeedPCIECfgState *s,
                                      const AspeedPCIECfgTxDesc *desc)
{
    AspeedPCIERcState *rc = &s->rc;
    PCIHostState *pci = NULL;
    PCIDevice *pdev = NULL;
    uint32_t cfg_addr;
    uint32_t offset;
    uint8_t byte_en;
    bool is_write;
    uint8_t devfn;
    uint64_t val;
    uint8_t bus;
    int len;

    val = ~0;
    is_write = !!(desc->desc0 & BIT(30));
    cfg_addr = desc->desc2;

    bus = (cfg_addr >> 24) & 0xff;
    devfn  = (cfg_addr >> 16) & 0xff;
    offset = cfg_addr & 0xffc;

    pci = PCI_HOST_BRIDGE(rc);

    /*
     * On the AST2600, the RC_H bus number range from 0x80 to 0xFF, with the
     * root device and root port assigned to bus 0x80 instead of the standard
     * 0x00. To allow the PCI subsystem to correctly discover devices on the
     * root bus, bus 0x80 is remapped to 0x00.
     */
    if (bus == rc->bus_nr) {
        bus = 0;
    }

    pdev = pci_find_device(pci->bus, bus, devfn);
    if (!pdev) {
        s->regs[desc->rdata_reg] = ~0;
        goto out;
    }

    switch (PCIE_CFG_FMTTYPE_MASK(desc->desc0)) {
    case TLP_FMTTYPE_CFGWR0:
    case TLP_FMTTYPE_CFGWR1:
        byte_en = PCIE_CFG_BYTE_EN(desc->desc1);
        val = desc->wdata;
        aspeed_pcie_cfg_translate_write(byte_en, &offset, &val, &len);
        pci_host_config_write_common(pdev, offset, pci_config_size(pdev),
                                     val, len);
        break;
    case TLP_FMTTYPE_CFGRD0:
    case TLP_FMTTYPE_CFGRD1:
        val = pci_host_config_read_common(pdev, offset,
                                          pci_config_size(pdev), 4);
        s->regs[desc->rdata_reg] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid CFG type. DESC0=0x%x\n",
                      __func__, desc->desc0);
    }

out:
    trace_aspeed_pcie_cfg_rw(s->id, is_write ?  "write" : "read", bus, devfn,
                             cfg_addr, val);
}

static void aspeed_pcie_cfg_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned int size)
{
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(opaque);
    AspeedPCIECfgTxDesc desc;
    uint32_t reg = addr >> 2;
    uint32_t rc_reg;

    trace_aspeed_pcie_cfg_write(s->id, addr, data);

    switch (reg) {
    case R_H2X_CTRL:
        if (data & R_H2X_CTRL_CLEAR_RX_MASK) {
            s->regs[R_H2X_RDATA] = ~0;
        }
        break;
    case R_H2X_TX_CLEAR:
        if (data & R_H2X_TX_CLEAR_IDLE_MASK) {
            s->regs[R_H2X_TX_STS] &= ~R_H2X_TX_STS_IDLE_MASK;
        }
        break;
    case R_H2X_TX_STS:
        if (data & R_H2X_TX_STS_TRIG_MASK) {
            desc.desc0 = s->regs[R_H2X_TX_DESC0];
            desc.desc1 = s->regs[R_H2X_TX_DESC1];
            desc.desc2 = s->regs[R_H2X_TX_DESC2];
            desc.desc3 = s->regs[R_H2X_TX_DESC3];
            desc.wdata = s->regs[R_H2X_TX_DATA];
            desc.rdata_reg = R_H2X_RC_H_RDATA;
            aspeed_pcie_cfg_readwrite(s, &desc);
            rc_reg = s->rc_regs->int_sts_reg;
            s->regs[rc_reg] |= H2X_RC_INT_INTDONE_MASK;
            s->regs[R_H2X_TX_STS] |=
                BIT(R_H2X_TX_STS_RC_H_TX_COMP_SHIFT);
            s->regs[R_H2X_TX_STS] |= R_H2X_TX_STS_IDLE_MASK;
        }
        break;
    /* preserve INTx status */
    case R_H2X_RC_H_INT_STS:
        if (data & H2X_RC_INT_INTDONE_MASK) {
            s->regs[R_H2X_TX_STS] &= ~R_H2X_TX_STS_RC_H_TX_COMP_MASK;
        }
        s->regs[reg] &= ~data | H2X_RC_INT_INTX_MASK;
        break;
    /*
     * These status registers are used for notify sources ISR are executed.
     * If one source ISR is executed, it will clear one bit.
     * If it clear all bits, it means to initialize this register status
     * rather than sources ISR are executed.
     */
    case R_H2X_RC_H_MSI_STS0:
    case R_H2X_RC_H_MSI_STS1:
        if (data == 0) {
            return ;
        }

        s->regs[reg] &= ~data;
        if (data == 0xffffffff) {
            return;
        }

        if (!s->regs[R_H2X_RC_H_MSI_STS0] &&
            !s->regs[R_H2X_RC_H_MSI_STS1]) {
            trace_aspeed_pcie_rc_msi_clear_irq(s->id, 0);
            qemu_set_irq(s->rc.irq, 0);
        }
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_pcie_cfg_ops = {
    .read = aspeed_pcie_cfg_read,
    .write = aspeed_pcie_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_pcie_cfg_instance_init(Object *obj)
{
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(obj);

    object_initialize_child(obj, "rc", &s->rc, TYPE_ASPEED_PCIE_RC);
    object_property_add_alias(obj, "dram", OBJECT(&s->rc), "dram");
    object_property_add_alias(obj, "dram-base", OBJECT(&s->rc), "dram-base");

    return;
}

static void aspeed_pcie_cfg_reset(DeviceState *dev)
{
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(dev);
    AspeedPCIECfgClass *apc = ASPEED_PCIE_CFG_GET_CLASS(s);

    memset(s->regs, 0, apc->nr_regs << 2);
    memset(s->tlpn_fifo, 0, sizeof(s->tlpn_fifo));
    s->tlpn_idx = 0;
}

static void aspeed_pcie_cfg_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(dev);
    AspeedPCIECfgClass *apc = ASPEED_PCIE_CFG_GET_CLASS(s);
    g_autofree char *name = NULL;

    s->rc_regs = &apc->reg_map->rc;
    s->regs = g_new(uint32_t, apc->nr_regs);
    name = g_strdup_printf(TYPE_ASPEED_PCIE_CFG ".regs.%d", s->id);
    memory_region_init_io(&s->mmio, OBJECT(s), apc->reg_ops, s, name,
                          apc->nr_regs << 2);
    sysbus_init_mmio(sbd, &s->mmio);

    object_property_set_int(OBJECT(&s->rc), "bus-nr",
                            apc->rc_bus_nr,
                            &error_abort);
    object_property_set_bool(OBJECT(&s->rc), "has-rd",
                            apc->rc_has_rd,
                            &error_abort);
    object_property_set_int(OBJECT(&s->rc), "rp-addr",
                            apc->rc_rp_addr,
                            &error_abort);
    object_property_set_int(OBJECT(&s->rc), "msi-addr",
                            apc->rc_msi_addr,
                            &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rc), errp)) {
        return;
    }
}

static void aspeed_pcie_cfg_unrealize(DeviceState *dev)
{
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(dev);

    g_free(s->regs);
    s->regs = NULL;
}

static const Property aspeed_pcie_cfg_props[] = {
    DEFINE_PROP_UINT32("id", AspeedPCIECfgState, id, 0),
};

static void aspeed_pcie_cfg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedPCIECfgClass *apc = ASPEED_PCIE_CFG_CLASS(klass);

    dc->desc = "ASPEED PCIe Config";
    dc->realize = aspeed_pcie_cfg_realize;
    dc->unrealize = aspeed_pcie_cfg_unrealize;
    device_class_set_legacy_reset(dc, aspeed_pcie_cfg_reset);
    device_class_set_props(dc, aspeed_pcie_cfg_props);

    apc->reg_ops = &aspeed_pcie_cfg_ops;
    apc->reg_map = &aspeed_regmap;
    apc->nr_regs = 0x100 >> 2;
    apc->rc_msi_addr = 0x1e77005C;
    apc->rc_bus_nr = 0x80;
    apc->rc_has_rd = true;
    apc->rc_rp_addr = PCI_DEVFN(8, 0);
}

static const TypeInfo aspeed_pcie_cfg_info = {
    .name       = TYPE_ASPEED_PCIE_CFG,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_pcie_cfg_instance_init,
    .instance_size = sizeof(AspeedPCIECfgState),
    .class_init = aspeed_pcie_cfg_class_init,
    .class_size = sizeof(AspeedPCIECfgClass),
};

static void aspeed_2700_pcie_cfg_write(void *opaque, hwaddr addr,
                                       uint64_t data, unsigned int size)
{
    AspeedPCIECfgState *s = ASPEED_PCIE_CFG(opaque);
    AspeedPCIECfgTxDesc desc;
    uint32_t reg = addr >> 2;

    trace_aspeed_pcie_cfg_write(s->id, addr, data);

    switch (reg) {
    case R_H2X_CFGE_INT_STS:
        if (data & R_H2X_CFGE_INT_STS_TX_IDEL_MASK) {
            s->regs[R_H2X_CFGE_INT_STS] &= ~R_H2X_CFGE_INT_STS_TX_IDEL_MASK;
        }

        if (data & R_H2X_CFGE_INT_STS_RX_BUSY_MASK) {
            s->regs[R_H2X_CFGE_INT_STS] &= ~R_H2X_CFGE_INT_STS_RX_BUSY_MASK;
        }
        break;
    case R_H2X_CFGI_CTRL:
        if (data & R_H2X_CFGI_CTRL_FIRE_MASK) {
            /*
             * Internal access to bridge
             * Type and BDF are 0
             */
            desc.desc0 = 0x04000001 |
                (ARRAY_FIELD_EX32(s->regs, H2X_CFGI_TLP, WR) << 30);
            desc.desc1 = 0x00401000 |
                ARRAY_FIELD_EX32(s->regs, H2X_CFGI_TLP, BEN);
            desc.desc2 = 0x00000000 |
                ARRAY_FIELD_EX32(s->regs, H2X_CFGI_TLP, ADDR);
            desc.wdata = s->regs[R_H2X_CFGI_WDATA];
            desc.rdata_reg = R_H2X_CFGI_RDATA;
            aspeed_pcie_cfg_readwrite(s, &desc);
        }
        break;
    case R_H2X_CFGE_TLPN:
        s->tlpn_fifo[s->tlpn_idx] = data;
        s->tlpn_idx = (s->tlpn_idx + 1) % ARRAY_SIZE(s->tlpn_fifo);
        break;
    case R_H2X_CFGE_CTRL:
        if (data & R_H2X_CFGE_CTRL_FIRE_MASK) {
            desc.desc0 = s->regs[R_H2X_CFGE_TLP1];
            desc.desc1 = s->tlpn_fifo[0];
            desc.desc2 = s->tlpn_fifo[1];
            desc.wdata = s->tlpn_fifo[2];
            desc.rdata_reg = R_H2X_CFGE_RDATA;
            aspeed_pcie_cfg_readwrite(s, &desc);
            s->regs[R_H2X_CFGE_INT_STS] |= R_H2X_CFGE_INT_STS_TX_IDEL_MASK;
            s->regs[R_H2X_CFGE_INT_STS] |= R_H2X_CFGE_INT_STS_RX_BUSY_MASK;
            s->tlpn_idx = 0;
        }
        break;

    case R_H2X_INT_STS:
        s->regs[reg] &= ~data | R_H2X_INT_STS_INTX_MASK;
        break;
    /*
     * These status registers are used for notify sources ISR are executed.
     * If one source ISR is executed, it will clear one bit.
     * If it clear all bits, it means to initialize this register status
     * rather than sources ISR are executed.
     */
    case R_H2X_MSI_STS0:
    case R_H2X_MSI_STS1:
        if (data == 0) {
            return ;
        }

        s->regs[reg] &= ~data;
        if (data == 0xffffffff) {
            return;
        }

        if (!s->regs[R_H2X_MSI_STS0] &&
            !s->regs[R_H2X_MSI_STS1]) {
            trace_aspeed_pcie_rc_msi_clear_irq(s->id, 0);
            qemu_set_irq(s->rc.irq, 0);
        }
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_2700_pcie_cfg_ops = {
    .read = aspeed_pcie_cfg_read,
    .write = aspeed_2700_pcie_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_2700_pcie_cfg_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedPCIECfgClass *apc = ASPEED_PCIE_CFG_CLASS(klass);

    dc->desc = "ASPEED 2700 PCIe Config";
    apc->reg_ops = &aspeed_2700_pcie_cfg_ops;
    apc->reg_map = &aspeed_2700_regmap;
    apc->nr_regs = 0x100 >> 2;
    apc->rc_msi_addr = 0x000000F0;
    apc->rc_bus_nr = 0;
    apc->rc_has_rd = false;
    apc->rc_rp_addr = PCI_DEVFN(0, 0);
}

static const TypeInfo aspeed_2700_pcie_cfg_info = {
    .name = TYPE_ASPEED_2700_PCIE_CFG,
    .parent = TYPE_ASPEED_PCIE_CFG,
    .class_init = aspeed_2700_pcie_cfg_class_init,
};

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

/* AST2700 */
REG32(PEHR_2700_LINK_GEN2,  0x344)
    FIELD(PEHR_2700_LINK_GEN2, STS, 18, 1)
REG32(PEHR_2700_LINK_GEN4,  0x358)
    FIELD(PEHR_2700_LINK_GEN4, STS, 8, 1)

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

static void aspeed_2700_pcie_phy_reset(DeviceState *dev)
{
    AspeedPCIEPhyState *s = ASPEED_PCIE_PHY(dev);
    AspeedPCIEPhyClass *apc = ASPEED_PCIE_PHY_GET_CLASS(s);

    memset(s->regs, 0, apc->nr_regs << 2);

    s->regs[R_PEHR_ID] =
        (0x1150 << R_PEHR_ID_DEV_SHIFT) | PCI_VENDOR_ID_ASPEED;
    s->regs[R_PEHR_CLASS_CODE] = 0x06040011;
    s->regs[R_PEHR_2700_LINK_GEN2] = R_PEHR_2700_LINK_GEN2_STS_MASK;
    s->regs[R_PEHR_2700_LINK_GEN4] = R_PEHR_2700_LINK_GEN4_STS_MASK;
}

static void aspeed_2700_pcie_phy_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedPCIEPhyClass *apc = ASPEED_PCIE_PHY_CLASS(klass);

    dc->desc = "ASPEED AST2700 PCIe Phy";
    device_class_set_legacy_reset(dc, aspeed_2700_pcie_phy_reset);

    apc->nr_regs = 0x800 >> 2;
}

static const TypeInfo aspeed_2700_pcie_phy_info = {
    .name       = TYPE_ASPEED_2700_PCIE_PHY,
    .parent     = TYPE_ASPEED_PCIE_PHY,
    .class_init = aspeed_2700_pcie_phy_class_init,
};

static void aspeed_pcie_register_types(void)
{
    type_register_static(&aspeed_pcie_rc_info);
    type_register_static(&aspeed_pcie_root_device_info);
    type_register_static(&aspeed_pcie_root_port_info);
    type_register_static(&aspeed_pcie_cfg_info);
    type_register_static(&aspeed_2700_pcie_cfg_info);
    type_register_static(&aspeed_pcie_phy_info);
    type_register_static(&aspeed_2700_pcie_phy_info);
}

type_init(aspeed_pcie_register_types);

