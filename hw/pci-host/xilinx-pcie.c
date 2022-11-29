/*
 * Xilinx PCIe host controller emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/pci/pci_bridge.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/pci-host/xilinx-pcie.h"

enum root_cfg_reg {
    /* Interrupt Decode Register */
    ROOTCFG_INTDEC              = 0x138,

    /* Interrupt Mask Register */
    ROOTCFG_INTMASK             = 0x13c,
    /* INTx Interrupt Received */
#define ROOTCFG_INTMASK_INTX    (1 << 16)
    /* MSI Interrupt Received */
#define ROOTCFG_INTMASK_MSI     (1 << 17)

    /* PHY Status/Control Register */
    ROOTCFG_PSCR                = 0x144,
    /* Link Up */
#define ROOTCFG_PSCR_LINK_UP    (1 << 11)

    /* Root Port Status/Control Register */
    ROOTCFG_RPSCR               = 0x148,
    /* Bridge Enable */
#define ROOTCFG_RPSCR_BRIDGEEN  (1 << 0)
    /* Interrupt FIFO Not Empty */
#define ROOTCFG_RPSCR_INTNEMPTY (1 << 18)
    /* Interrupt FIFO Overflow */
#define ROOTCFG_RPSCR_INTOVF    (1 << 19)

    /* Root Port Interrupt FIFO Read Register 1 */
    ROOTCFG_RPIFR1              = 0x158,
#define ROOTCFG_RPIFR1_INT_LANE_SHIFT   27
#define ROOTCFG_RPIFR1_INT_ASSERT_SHIFT 29
#define ROOTCFG_RPIFR1_INT_VALID_SHIFT  31
    /* Root Port Interrupt FIFO Read Register 2 */
    ROOTCFG_RPIFR2              = 0x15c,
};

static void xilinx_pcie_update_intr(XilinxPCIEHost *s,
                                    uint32_t set, uint32_t clear)
{
    int level;

    s->intr |= set;
    s->intr &= ~clear;

    if (s->intr_fifo_r != s->intr_fifo_w) {
        s->intr |= ROOTCFG_INTMASK_INTX;
    }

    level = !!(s->intr & s->intr_mask);
    qemu_set_irq(s->irq, level);
}

static void xilinx_pcie_queue_intr(XilinxPCIEHost *s,
                                   uint32_t fifo_reg1, uint32_t fifo_reg2)
{
    XilinxPCIEInt *intr;
    unsigned int new_w;

    new_w = (s->intr_fifo_w + 1) % ARRAY_SIZE(s->intr_fifo);
    if (new_w == s->intr_fifo_r) {
        s->rpscr |= ROOTCFG_RPSCR_INTOVF;
        return;
    }

    intr = &s->intr_fifo[s->intr_fifo_w];
    s->intr_fifo_w = new_w;

    intr->fifo_reg1 = fifo_reg1;
    intr->fifo_reg2 = fifo_reg2;

    xilinx_pcie_update_intr(s, ROOTCFG_INTMASK_INTX, 0);
}

static void xilinx_pcie_set_irq(void *opaque, int irq_num, int level)
{
    XilinxPCIEHost *s = XILINX_PCIE_HOST(opaque);

    xilinx_pcie_queue_intr(s,
       (irq_num << ROOTCFG_RPIFR1_INT_LANE_SHIFT) |
           (level << ROOTCFG_RPIFR1_INT_ASSERT_SHIFT) |
           (1 << ROOTCFG_RPIFR1_INT_VALID_SHIFT),
       0);
}

static void xilinx_pcie_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    XilinxPCIEHost *s = XILINX_PCIE_HOST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    PCIExpressHost *pex = PCIE_HOST_BRIDGE(dev);

    snprintf(s->name, sizeof(s->name), "pcie%u", s->bus_nr);

    /* PCI configuration space */
    pcie_host_mmcfg_init(pex, s->cfg_size);

    /* MMIO region */
    memory_region_init(&s->mmio, OBJECT(s), "mmio", UINT64_MAX);
    memory_region_set_enabled(&s->mmio, false);

    /* dummy PCI I/O region (not visible to the CPU) */
    memory_region_init(&s->io, OBJECT(s), "io", 16);

    /* interrupt out */
    qdev_init_gpio_out_named(dev, &s->irq, "interrupt_out", 1);

    sysbus_init_mmio(sbd, &pex->mmio);
    sysbus_init_mmio(sbd, &s->mmio);

    pci->bus = pci_register_root_bus(dev, s->name, xilinx_pcie_set_irq,
                                     pci_swizzle_map_irq_fn, s, &s->mmio,
                                     &s->io, 0, 4, TYPE_PCIE_BUS);

    qdev_realize(DEVICE(&s->root), BUS(pci->bus), &error_fatal);
}

static const char *xilinx_pcie_host_root_bus_path(PCIHostState *host_bridge,
                                                  PCIBus *rootbus)
{
    return "0000:00";
}

static void xilinx_pcie_host_init(Object *obj)
{
    XilinxPCIEHost *s = XILINX_PCIE_HOST(obj);
    XilinxPCIERoot *root = &s->root;

    object_initialize_child(obj, "root", root, TYPE_XILINX_PCIE_ROOT);
    qdev_prop_set_int32(DEVICE(root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(root), "multifunction", false);
}

static Property xilinx_pcie_host_props[] = {
    DEFINE_PROP_UINT32("bus_nr", XilinxPCIEHost, bus_nr, 0),
    DEFINE_PROP_SIZE("cfg_base", XilinxPCIEHost, cfg_base, 0),
    DEFINE_PROP_SIZE("cfg_size", XilinxPCIEHost, cfg_size, 32 * MiB),
    DEFINE_PROP_SIZE("mmio_base", XilinxPCIEHost, mmio_base, 0),
    DEFINE_PROP_SIZE("mmio_size", XilinxPCIEHost, mmio_size, 1 * MiB),
    DEFINE_PROP_BOOL("link_up", XilinxPCIEHost, link_up, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_pcie_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = xilinx_pcie_host_root_bus_path;
    dc->realize = xilinx_pcie_host_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
    device_class_set_props(dc, xilinx_pcie_host_props);
}

static const TypeInfo xilinx_pcie_host_info = {
    .name       = TYPE_XILINX_PCIE_HOST,
    .parent     = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(XilinxPCIEHost),
    .instance_init = xilinx_pcie_host_init,
    .class_init = xilinx_pcie_host_class_init,
};

static uint32_t xilinx_pcie_root_config_read(PCIDevice *d,
                                             uint32_t address, int len)
{
    XilinxPCIEHost *s = XILINX_PCIE_HOST(OBJECT(d)->parent);
    uint32_t val;

    switch (address) {
    case ROOTCFG_INTDEC:
        val = s->intr;
        break;
    case ROOTCFG_INTMASK:
        val = s->intr_mask;
        break;
    case ROOTCFG_PSCR:
        val = s->link_up ? ROOTCFG_PSCR_LINK_UP : 0;
        break;
    case ROOTCFG_RPSCR:
        if (s->intr_fifo_r != s->intr_fifo_w) {
            s->rpscr &= ~ROOTCFG_RPSCR_INTNEMPTY;
        } else {
            s->rpscr |= ROOTCFG_RPSCR_INTNEMPTY;
        }
        val = s->rpscr;
        break;
    case ROOTCFG_RPIFR1:
        if (s->intr_fifo_w == s->intr_fifo_r) {
            /* FIFO empty */
            val = 0;
        } else {
            val = s->intr_fifo[s->intr_fifo_r].fifo_reg1;
        }
        break;
    case ROOTCFG_RPIFR2:
        if (s->intr_fifo_w == s->intr_fifo_r) {
            /* FIFO empty */
            val = 0;
        } else {
            val = s->intr_fifo[s->intr_fifo_r].fifo_reg2;
        }
        break;
    default:
        val = pci_default_read_config(d, address, len);
        break;
    }
    return val;
}

static void xilinx_pcie_root_config_write(PCIDevice *d, uint32_t address,
                                          uint32_t val, int len)
{
    XilinxPCIEHost *s = XILINX_PCIE_HOST(OBJECT(d)->parent);
    switch (address) {
    case ROOTCFG_INTDEC:
        xilinx_pcie_update_intr(s, 0, val);
        break;
    case ROOTCFG_INTMASK:
        s->intr_mask = val;
        xilinx_pcie_update_intr(s, 0, 0);
        break;
    case ROOTCFG_RPSCR:
        s->rpscr &= ~ROOTCFG_RPSCR_BRIDGEEN;
        s->rpscr |= val & ROOTCFG_RPSCR_BRIDGEEN;
        memory_region_set_enabled(&s->mmio, val & ROOTCFG_RPSCR_BRIDGEEN);

        if (val & ROOTCFG_INTMASK_INTX) {
            s->rpscr &= ~ROOTCFG_INTMASK_INTX;
        }
        break;
    case ROOTCFG_RPIFR1:
    case ROOTCFG_RPIFR2:
        if (s->intr_fifo_w == s->intr_fifo_r) {
            /* FIFO empty */
            return;
        } else {
            s->intr_fifo_r = (s->intr_fifo_r + 1) % ARRAY_SIZE(s->intr_fifo);
        }
        break;
    default:
        pci_default_write_config(d, address, val, len);
        break;
    }
}

static void xilinx_pcie_root_realize(PCIDevice *pci_dev, Error **errp)
{
    BusState *bus = qdev_get_parent_bus(DEVICE(pci_dev));
    XilinxPCIEHost *s = XILINX_PCIE_HOST(bus->parent);

    pci_set_word(pci_dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_set_word(pci_dev->config + PCI_MEMORY_BASE, s->mmio_base >> 16);
    pci_set_word(pci_dev->config + PCI_MEMORY_LIMIT,
                 ((s->mmio_base + s->mmio_size - 1) >> 16) & 0xfff0);

    pci_bridge_initfn(pci_dev, TYPE_PCI_BUS);

    if (pcie_endpoint_cap_v1_init(pci_dev, 0x80) < 0) {
        error_setg(errp, "Failed to initialize PCIe capability");
    }
}

static void xilinx_pcie_root_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Xilinx AXI-PCIe Host Bridge";
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = 0x7021;
    k->revision = 0;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    k->realize = xilinx_pcie_root_realize;
    k->exit = pci_bridge_exitfn;
    dc->reset = pci_bridge_reset;
    k->config_read = xilinx_pcie_root_config_read;
    k->config_write = xilinx_pcie_root_config_write;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo xilinx_pcie_root_info = {
    .name = TYPE_XILINX_PCIE_ROOT,
    .parent = TYPE_PCI_BRIDGE,
    .instance_size = sizeof(XilinxPCIERoot),
    .class_init = xilinx_pcie_root_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void xilinx_pcie_register(void)
{
    type_register_static(&xilinx_pcie_root_info);
    type_register_static(&xilinx_pcie_host_info);
}

type_init(xilinx_pcie_register)
