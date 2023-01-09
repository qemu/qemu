/*
 * Marvell Discovery II MV64361 System Controller for
 * QEMU PowerPC CHRP (Genesi/bPlan Pegasos II) hardware System Emulator
 *
 * Copyright (c) 2018-2020 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "hw/irq.h"
#include "hw/intc/i8259.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/pci-host/mv64361.h"
#include "mv643xx.h"

#define TYPE_MV64361_PCI_BRIDGE "mv64361-pcibridge"

static void mv64361_pcibridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_MARVELL;
    k->device_id = PCI_DEVICE_ID_MARVELL_MV6436X;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge,
     * not usable without the host-facing part
     */
    dc->user_creatable = false;
}

static const TypeInfo mv64361_pcibridge_info = {
    .name          = TYPE_MV64361_PCI_BRIDGE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = mv64361_pcibridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};


#define TYPE_MV64361_PCI "mv64361-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(MV64361PCIState, MV64361_PCI)

struct MV64361PCIState {
    PCIHostState parent_obj;

    uint8_t index;
    MemoryRegion io;
    MemoryRegion mem;
    qemu_irq irq[PCI_NUM_PINS];

    uint32_t io_base;
    uint32_t io_size;
    uint32_t mem_base[4];
    uint32_t mem_size[4];
    uint64_t remap[5];
};

static int mv64361_pcihost_map_irq(PCIDevice *pci_dev, int n)
{
    return (n + PCI_SLOT(pci_dev->devfn)) % PCI_NUM_PINS;
}

static void mv64361_pcihost_set_irq(void *opaque, int n, int level)
{
    MV64361PCIState *s = opaque;
    qemu_set_irq(s->irq[n], level);
}

static void mv64361_pcihost_realize(DeviceState *dev, Error **errp)
{
    MV64361PCIState *s = MV64361_PCI(dev);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    char *name;

    name = g_strdup_printf("pci%d-io", s->index);
    memory_region_init(&s->io, OBJECT(dev), name, 0x10000);
    g_free(name);
    name = g_strdup_printf("pci%d-mem", s->index);
    memory_region_init(&s->mem, OBJECT(dev), name, 1ULL << 32);
    g_free(name);
    name = g_strdup_printf("pci.%d", s->index);
    h->bus = pci_register_root_bus(dev, name, mv64361_pcihost_set_irq,
                                   mv64361_pcihost_map_irq, dev,
                                   &s->mem, &s->io, 0, 4, TYPE_PCI_BUS);
    g_free(name);
    pci_create_simple(h->bus, 0, TYPE_MV64361_PCI_BRIDGE);
}

static Property mv64361_pcihost_props[] = {
    DEFINE_PROP_UINT8("index", MV64361PCIState, index, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void mv64361_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mv64361_pcihost_realize;
    device_class_set_props(dc, mv64361_pcihost_props);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo mv64361_pcihost_info = {
       .name          = TYPE_MV64361_PCI,
       .parent        = TYPE_PCI_HOST_BRIDGE,
       .instance_size = sizeof(MV64361PCIState),
       .class_init    = mv64361_pcihost_class_init,
};

static void mv64361_pci_register_types(void)
{
   type_register_static(&mv64361_pcihost_info);
   type_register_static(&mv64361_pcibridge_info);
}

type_init(mv64361_pci_register_types)


OBJECT_DECLARE_SIMPLE_TYPE(MV64361State, MV64361)

struct MV64361State {
    SysBusDevice parent_obj;

    MemoryRegion regs;
    MV64361PCIState pci[2];
    MemoryRegion cpu_win[19];
    qemu_irq cpu_irq;

    /* registers state */
    uint32_t cpu_conf;
    uint32_t regs_base;
    uint32_t base_addr_enable;
    uint64_t main_int_cr;
    uint64_t cpu0_int_mask;
    uint32_t gpp_io;
    uint32_t gpp_level;
    uint32_t gpp_value;
    uint32_t gpp_int_cr;
    uint32_t gpp_int_mask;
    bool gpp_int_level;
};

enum mv64361_irq_cause {
    MV64361_IRQ_DEVERR = 1,
    MV64361_IRQ_DMAERR = 2,
    MV64361_IRQ_CPUERR = 3,
    MV64361_IRQ_IDMA0 = 4,
    MV64361_IRQ_IDMA1 = 5,
    MV64361_IRQ_IDMA2 = 6,
    MV64361_IRQ_IDMA3 = 7,
    MV64361_IRQ_TIMER0 = 8,
    MV64361_IRQ_TIMER1 = 9,
    MV64361_IRQ_TIMER2 = 10,
    MV64361_IRQ_TIMER3 = 11,
    MV64361_IRQ_PCI0 = 12,
    MV64361_IRQ_SRAMERR = 13,
    MV64361_IRQ_GBEERR = 14,
    MV64361_IRQ_CERR = 15,
    MV64361_IRQ_PCI1 = 16,
    MV64361_IRQ_DRAMERR = 17,
    MV64361_IRQ_WDNMI = 18,
    MV64361_IRQ_WDE = 19,
    MV64361_IRQ_PCI0IN = 20,
    MV64361_IRQ_PCI0OUT = 21,
    MV64361_IRQ_PCI1IN = 22,
    MV64361_IRQ_PCI1OUT = 23,
    MV64361_IRQ_P1_GPP0_7 = 24,
    MV64361_IRQ_P1_GPP8_15 = 25,
    MV64361_IRQ_P1_GPP16_23 = 26,
    MV64361_IRQ_P1_GPP24_31 = 27,
    MV64361_IRQ_P1_CPU_DB = 28,
    /* 29-31: reserved */
    MV64361_IRQ_GBE0 = 32,
    MV64361_IRQ_GBE1 = 33,
    MV64361_IRQ_GBE2 = 34,
    /* 35: reserved */
    MV64361_IRQ_SDMA0 = 36,
    MV64361_IRQ_TWSI = 37,
    MV64361_IRQ_SDMA1 = 38,
    MV64361_IRQ_BRG = 39,
    MV64361_IRQ_MPSC0 = 40,
    MV64361_IRQ_MPSC1 = 41,
    MV64361_IRQ_G0RX = 42,
    MV64361_IRQ_G0TX = 43,
    MV64361_IRQ_G0MISC = 44,
    MV64361_IRQ_G1RX = 45,
    MV64361_IRQ_G1TX = 46,
    MV64361_IRQ_G1MISC = 47,
    MV64361_IRQ_G2RX = 48,
    MV64361_IRQ_G2TX = 49,
    MV64361_IRQ_G2MISC = 50,
    /* 51-55: reserved */
    MV64361_IRQ_P0_GPP0_7 = 56,
    MV64361_IRQ_P0_GPP8_15 = 57,
    MV64361_IRQ_P0_GPP16_23 = 58,
    MV64361_IRQ_P0_GPP24_31 = 59,
    MV64361_IRQ_P0_CPU_DB = 60,
    /* 61-63: reserved */
};

PCIBus *mv64361_get_pci_bus(DeviceState *dev, int n)
{
    MV64361State *mv = MV64361(dev);
    return PCI_HOST_BRIDGE(&mv->pci[n])->bus;
}

static void unmap_region(MemoryRegion *mr)
{
    if (memory_region_is_mapped(mr)) {
        memory_region_del_subregion(get_system_memory(), mr);
        object_unparent(OBJECT(mr));
    }
}

static void map_pci_region(MemoryRegion *mr, MemoryRegion *parent,
                           struct Object *owner, const char *name,
                           hwaddr poffs, uint64_t size, hwaddr moffs)
{
    memory_region_init_alias(mr, owner, name, parent, poffs, size);
    memory_region_add_subregion(get_system_memory(), moffs, mr);
    trace_mv64361_region_map(name, poffs, size, moffs);
}

static void set_mem_windows(MV64361State *s, uint32_t val)
{
    MV64361PCIState *p;
    MemoryRegion *mr;
    uint32_t mask;
    int i;

    val &= 0x1fffff;
    for (mask = 1, i = 0; i < 21; i++, mask <<= 1) {
        if ((val & mask) != (s->base_addr_enable & mask)) {
            trace_mv64361_region_enable(!(val & mask) ? "enable" : "disable", i);
            /*
             * 0-3 are SDRAM chip selects but we map all RAM directly
             * 4-7 are device chip selects (not sure what those are)
             * 8 is Boot device (ROM) chip select but we map that directly too
             */
            if (i == 9) {
                p = &s->pci[0];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->io, OBJECT(s), "pci0-io-win",
                                   p->remap[4], (p->io_size + 1) << 16,
                                   (p->io_base & 0xfffff) << 16);
                }
            } else if (i == 10) {
                p = &s->pci[0];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci0-mem0-win",
                                   p->remap[0], (p->mem_size[0] + 1) << 16,
                                   (p->mem_base[0] & 0xfffff) << 16);
                }
            } else if (i == 11) {
                p = &s->pci[0];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci0-mem1-win",
                                   p->remap[1], (p->mem_size[1] + 1) << 16,
                                   (p->mem_base[1] & 0xfffff) << 16);
                }
            } else if (i == 12) {
                p = &s->pci[0];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci0-mem2-win",
                                   p->remap[2], (p->mem_size[2] + 1) << 16,
                                   (p->mem_base[2] & 0xfffff) << 16);
                }
            } else if (i == 13) {
                p = &s->pci[0];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci0-mem3-win",
                                   p->remap[3], (p->mem_size[3] + 1) << 16,
                                   (p->mem_base[3] & 0xfffff) << 16);
                }
            } else if (i == 14) {
                p = &s->pci[1];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->io, OBJECT(s), "pci1-io-win",
                                   p->remap[4], (p->io_size + 1) << 16,
                                   (p->io_base & 0xfffff) << 16);
                }
            } else if (i == 15) {
                p = &s->pci[1];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci1-mem0-win",
                                   p->remap[0], (p->mem_size[0] + 1) << 16,
                                   (p->mem_base[0] & 0xfffff) << 16);
                }
            } else if (i == 16) {
                p = &s->pci[1];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci1-mem1-win",
                                   p->remap[1], (p->mem_size[1] + 1) << 16,
                                   (p->mem_base[1] & 0xfffff) << 16);
                }
            } else if (i == 17) {
                p = &s->pci[1];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci1-mem2-win",
                                   p->remap[2], (p->mem_size[2] + 1) << 16,
                                   (p->mem_base[2] & 0xfffff) << 16);
                }
            } else if (i == 18) {
                p = &s->pci[1];
                mr = &s->cpu_win[i];
                unmap_region(mr);
                if (!(val & mask)) {
                    map_pci_region(mr, &p->mem, OBJECT(s), "pci1-mem3-win",
                                   p->remap[3], (p->mem_size[3] + 1) << 16,
                                   (p->mem_base[3] & 0xfffff) << 16);
                }
            /* 19 is integrated SRAM */
            } else if (i == 20) {
                mr = &s->regs;
                unmap_region(mr);
                if (!(val & mask)) {
                    memory_region_add_subregion(get_system_memory(),
                        (s->regs_base & 0xfffff) << 16, mr);
                }
            }
        }
    }
    s->base_addr_enable = val;
}

static void mv64361_update_irq(void *opaque, int n, int level)
{
    MV64361State *s = opaque;
    uint64_t val = s->main_int_cr;

    if (level) {
        val |= BIT_ULL(n);
    } else {
        val &= ~BIT_ULL(n);
    }
    if ((s->main_int_cr & s->cpu0_int_mask) != (val & s->cpu0_int_mask)) {
        qemu_set_irq(s->cpu_irq, level);
    }
    s->main_int_cr = val;
}

static uint64_t mv64361_read(void *opaque, hwaddr addr, unsigned int size)
{
    MV64361State *s = MV64361(opaque);
    uint32_t ret = 0;

    switch (addr) {
    case MV64340_CPU_CONFIG:
        ret = s->cpu_conf;
        break;
    case MV64340_PCI_0_IO_BASE_ADDR:
        ret = s->pci[0].io_base;
        break;
    case MV64340_PCI_0_IO_SIZE:
        ret = s->pci[0].io_size;
        break;
    case MV64340_PCI_0_IO_ADDR_REMAP:
        ret = s->pci[0].remap[4] >> 16;
        break;
    case MV64340_PCI_0_MEMORY0_BASE_ADDR:
        ret = s->pci[0].mem_base[0];
        break;
    case MV64340_PCI_0_MEMORY0_SIZE:
        ret = s->pci[0].mem_size[0];
        break;
    case MV64340_PCI_0_MEMORY0_LOW_ADDR_REMAP:
        ret = (s->pci[0].remap[0] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_0_MEMORY0_HIGH_ADDR_REMAP:
        ret = s->pci[0].remap[0] >> 32;
        break;
    case MV64340_PCI_0_MEMORY1_BASE_ADDR:
        ret = s->pci[0].mem_base[1];
        break;
    case MV64340_PCI_0_MEMORY1_SIZE:
        ret = s->pci[0].mem_size[1];
        break;
    case MV64340_PCI_0_MEMORY1_LOW_ADDR_REMAP:
        ret = (s->pci[0].remap[1] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_0_MEMORY1_HIGH_ADDR_REMAP:
        ret = s->pci[0].remap[1] >> 32;
        break;
    case MV64340_PCI_0_MEMORY2_BASE_ADDR:
        ret = s->pci[0].mem_base[2];
        break;
    case MV64340_PCI_0_MEMORY2_SIZE:
        ret = s->pci[0].mem_size[2];
        break;
    case MV64340_PCI_0_MEMORY2_LOW_ADDR_REMAP:
        ret = (s->pci[0].remap[2] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_0_MEMORY2_HIGH_ADDR_REMAP:
        ret = s->pci[0].remap[2] >> 32;
        break;
    case MV64340_PCI_0_MEMORY3_BASE_ADDR:
        ret = s->pci[0].mem_base[3];
        break;
    case MV64340_PCI_0_MEMORY3_SIZE:
        ret = s->pci[0].mem_size[3];
        break;
    case MV64340_PCI_0_MEMORY3_LOW_ADDR_REMAP:
        ret = (s->pci[0].remap[3] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_0_MEMORY3_HIGH_ADDR_REMAP:
        ret = s->pci[0].remap[3] >> 32;
        break;
    case MV64340_PCI_1_IO_BASE_ADDR:
        ret = s->pci[1].io_base;
        break;
    case MV64340_PCI_1_IO_SIZE:
        ret = s->pci[1].io_size;
        break;
    case MV64340_PCI_1_IO_ADDR_REMAP:
        ret = s->pci[1].remap[4] >> 16;
        break;
    case MV64340_PCI_1_MEMORY0_BASE_ADDR:
        ret = s->pci[1].mem_base[0];
        break;
    case MV64340_PCI_1_MEMORY0_SIZE:
        ret = s->pci[1].mem_size[0];
        break;
    case MV64340_PCI_1_MEMORY0_LOW_ADDR_REMAP:
        ret = (s->pci[1].remap[0] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_1_MEMORY0_HIGH_ADDR_REMAP:
        ret = s->pci[1].remap[0] >> 32;
        break;
    case MV64340_PCI_1_MEMORY1_BASE_ADDR:
        ret = s->pci[1].mem_base[1];
        break;
    case MV64340_PCI_1_MEMORY1_SIZE:
        ret = s->pci[1].mem_size[1];
        break;
    case MV64340_PCI_1_MEMORY1_LOW_ADDR_REMAP:
        ret = (s->pci[1].remap[1] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_1_MEMORY1_HIGH_ADDR_REMAP:
        ret = s->pci[1].remap[1] >> 32;
        break;
    case MV64340_PCI_1_MEMORY2_BASE_ADDR:
        ret = s->pci[1].mem_base[2];
        break;
    case MV64340_PCI_1_MEMORY2_SIZE:
        ret = s->pci[1].mem_size[2];
        break;
    case MV64340_PCI_1_MEMORY2_LOW_ADDR_REMAP:
        ret = (s->pci[1].remap[2] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_1_MEMORY2_HIGH_ADDR_REMAP:
        ret = s->pci[1].remap[2] >> 32;
        break;
    case MV64340_PCI_1_MEMORY3_BASE_ADDR:
        ret = s->pci[1].mem_base[3];
        break;
    case MV64340_PCI_1_MEMORY3_SIZE:
        ret = s->pci[1].mem_size[3];
        break;
    case MV64340_PCI_1_MEMORY3_LOW_ADDR_REMAP:
        ret = (s->pci[1].remap[3] & 0xffff0000) >> 16;
        break;
    case MV64340_PCI_1_MEMORY3_HIGH_ADDR_REMAP:
        ret = s->pci[1].remap[3] >> 32;
        break;
    case MV64340_INTERNAL_SPACE_BASE_ADDR:
        ret = s->regs_base;
        break;
    case MV64340_BASE_ADDR_ENABLE:
        ret = s->base_addr_enable;
        break;
    case MV64340_PCI_0_CONFIG_ADDR:
        ret = pci_host_conf_le_ops.read(PCI_HOST_BRIDGE(&s->pci[0]), 0, size);
        break;
    case MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG ...
         MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG + 3:
        ret = pci_host_data_le_ops.read(PCI_HOST_BRIDGE(&s->pci[0]),
                  addr - MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG, size);
        break;
    case MV64340_PCI_1_CONFIG_ADDR:
        ret = pci_host_conf_le_ops.read(PCI_HOST_BRIDGE(&s->pci[1]), 0, size);
        break;
    case MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG ...
         MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG + 3:
        ret = pci_host_data_le_ops.read(PCI_HOST_BRIDGE(&s->pci[1]),
                  addr - MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG, size);
        break;
    case MV64340_PCI_1_INTERRUPT_ACKNOWLEDGE_VIRTUAL_REG:
        /* FIXME: Should this be sent via the PCI bus somehow? */
        if (s->gpp_int_level && (s->gpp_value & BIT(31))) {
            ret = pic_read_irq(isa_pic);
        }
        break;
    case MV64340_MAIN_INTERRUPT_CAUSE_LOW:
        ret = s->main_int_cr;
        break;
    case MV64340_MAIN_INTERRUPT_CAUSE_HIGH:
        ret = s->main_int_cr >> 32;
        break;
    case MV64340_CPU_INTERRUPT0_MASK_LOW:
        ret = s->cpu0_int_mask;
        break;
    case MV64340_CPU_INTERRUPT0_MASK_HIGH:
        ret = s->cpu0_int_mask >> 32;
        break;
    case MV64340_CPU_INTERRUPT0_SELECT_CAUSE:
        ret = s->main_int_cr;
        if (s->main_int_cr & s->cpu0_int_mask) {
            if (!(s->main_int_cr & s->cpu0_int_mask & 0xffffffff)) {
                ret = s->main_int_cr >> 32 | BIT(30);
            } else if ((s->main_int_cr & s->cpu0_int_mask) >> 32) {
                ret |= BIT(31);
            }
        }
        break;
    case MV64340_CUNIT_ARBITER_CONTROL_REG:
        ret = 0x11ff0000 | (s->gpp_int_level << 10);
        break;
    case MV64340_GPP_IO_CONTROL:
        ret = s->gpp_io;
        break;
    case MV64340_GPP_LEVEL_CONTROL:
        ret = s->gpp_level;
        break;
    case MV64340_GPP_VALUE:
        ret = s->gpp_value;
        break;
    case MV64340_GPP_VALUE_SET:
    case MV64340_GPP_VALUE_CLEAR:
        ret = 0;
        break;
    case MV64340_GPP_INTERRUPT_CAUSE:
        ret = s->gpp_int_cr;
        break;
    case MV64340_GPP_INTERRUPT_MASK0:
    case MV64340_GPP_INTERRUPT_MASK1:
        ret = s->gpp_int_mask;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented register read 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    if (addr != MV64340_PCI_1_INTERRUPT_ACKNOWLEDGE_VIRTUAL_REG) {
        trace_mv64361_reg_read(addr, ret);
    }
    return ret;
}

static void warn_swap_bit(uint64_t val)
{
    if ((val & 0x3000000ULL) >> 24 != 1) {
        qemu_log_mask(LOG_UNIMP, "%s: Data swap not implemented", __func__);
    }
}

static void mv64361_set_pci_mem_remap(MV64361State *s, int bus, int idx,
                                      uint64_t val, bool high)
{
    if (high) {
        s->pci[bus].remap[idx] = val;
    } else {
        s->pci[bus].remap[idx] &= 0xffffffff00000000ULL;
        s->pci[bus].remap[idx] |= (val & 0xffffULL) << 16;
    }
}

static void mv64361_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned int size)
{
    MV64361State *s = MV64361(opaque);

    trace_mv64361_reg_write(addr, val);
    switch (addr) {
    case MV64340_CPU_CONFIG:
        s->cpu_conf = val & 0xe4e3bffULL;
        s->cpu_conf |= BIT(23);
        break;
    case MV64340_PCI_0_IO_BASE_ADDR:
        s->pci[0].io_base = val & 0x30fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            s->pci[0].remap[4] = (val & 0xffffULL) << 16;
        }
        break;
    case MV64340_PCI_0_IO_SIZE:
        s->pci[0].io_size = val & 0xffffULL;
        break;
    case MV64340_PCI_0_IO_ADDR_REMAP:
        s->pci[0].remap[4] = (val & 0xffffULL) << 16;
        break;
    case MV64340_PCI_0_MEMORY0_BASE_ADDR:
        s->pci[0].mem_base[0] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 0, 0, val, false);
        }
        break;
    case MV64340_PCI_0_MEMORY0_SIZE:
        s->pci[0].mem_size[0] = val & 0xffffULL;
        break;
    case MV64340_PCI_0_MEMORY0_LOW_ADDR_REMAP:
    case MV64340_PCI_0_MEMORY0_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 0, 0, val,
            (addr == MV64340_PCI_0_MEMORY0_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_0_MEMORY1_BASE_ADDR:
        s->pci[0].mem_base[1] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 0, 1, val, false);
        }
        break;
    case MV64340_PCI_0_MEMORY1_SIZE:
        s->pci[0].mem_size[1] = val & 0xffffULL;
        break;
    case MV64340_PCI_0_MEMORY1_LOW_ADDR_REMAP:
    case MV64340_PCI_0_MEMORY1_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 0, 1, val,
            (addr == MV64340_PCI_0_MEMORY1_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_0_MEMORY2_BASE_ADDR:
        s->pci[0].mem_base[2] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 0, 2, val, false);
        }
        break;
    case MV64340_PCI_0_MEMORY2_SIZE:
        s->pci[0].mem_size[2] = val & 0xffffULL;
        break;
    case MV64340_PCI_0_MEMORY2_LOW_ADDR_REMAP:
    case MV64340_PCI_0_MEMORY2_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 0, 2, val,
            (addr == MV64340_PCI_0_MEMORY2_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_0_MEMORY3_BASE_ADDR:
        s->pci[0].mem_base[3] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 0, 3, val, false);
        }
        break;
    case MV64340_PCI_0_MEMORY3_SIZE:
        s->pci[0].mem_size[3] = val & 0xffffULL;
        break;
    case MV64340_PCI_0_MEMORY3_LOW_ADDR_REMAP:
    case MV64340_PCI_0_MEMORY3_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 0, 3, val,
            (addr == MV64340_PCI_0_MEMORY3_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_1_IO_BASE_ADDR:
        s->pci[1].io_base = val & 0x30fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            s->pci[1].remap[4] = (val & 0xffffULL) << 16;
        }
        break;
    case MV64340_PCI_1_IO_SIZE:
        s->pci[1].io_size = val & 0xffffULL;
        break;
    case MV64340_PCI_1_MEMORY0_BASE_ADDR:
        s->pci[1].mem_base[0] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 1, 0, val, false);
        }
        break;
    case MV64340_PCI_1_MEMORY0_SIZE:
        s->pci[1].mem_size[0] = val & 0xffffULL;
        break;
    case MV64340_PCI_1_MEMORY0_LOW_ADDR_REMAP:
    case MV64340_PCI_1_MEMORY0_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 1, 0, val,
            (addr == MV64340_PCI_1_MEMORY0_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_1_MEMORY1_BASE_ADDR:
        s->pci[1].mem_base[1] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 1, 1, val, false);
        }
        break;
    case MV64340_PCI_1_MEMORY1_SIZE:
        s->pci[1].mem_size[1] = val & 0xffffULL;
        break;
    case MV64340_PCI_1_MEMORY1_LOW_ADDR_REMAP:
    case MV64340_PCI_1_MEMORY1_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 1, 1, val,
            (addr == MV64340_PCI_1_MEMORY1_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_1_MEMORY2_BASE_ADDR:
        s->pci[1].mem_base[2] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 1, 2, val, false);
        }
        break;
    case MV64340_PCI_1_MEMORY2_SIZE:
        s->pci[1].mem_size[2] = val & 0xffffULL;
        break;
    case MV64340_PCI_1_MEMORY2_LOW_ADDR_REMAP:
    case MV64340_PCI_1_MEMORY2_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 1, 2, val,
            (addr == MV64340_PCI_1_MEMORY2_HIGH_ADDR_REMAP));
        break;
    case MV64340_PCI_1_MEMORY3_BASE_ADDR:
        s->pci[1].mem_base[3] = val & 0x70fffffULL;
        warn_swap_bit(val);
        if (!(s->cpu_conf & BIT(27))) {
            mv64361_set_pci_mem_remap(s, 1, 3, val, false);
        }
        break;
    case MV64340_PCI_1_MEMORY3_SIZE:
        s->pci[1].mem_size[3] = val & 0xffffULL;
        break;
    case MV64340_PCI_1_MEMORY3_LOW_ADDR_REMAP:
    case MV64340_PCI_1_MEMORY3_HIGH_ADDR_REMAP:
        mv64361_set_pci_mem_remap(s, 1, 3, val,
            (addr == MV64340_PCI_1_MEMORY3_HIGH_ADDR_REMAP));
        break;
    case MV64340_INTERNAL_SPACE_BASE_ADDR:
        s->regs_base = val & 0xfffffULL;
        break;
    case MV64340_BASE_ADDR_ENABLE:
        set_mem_windows(s, val);
        break;
    case MV64340_PCI_0_CONFIG_ADDR:
        pci_host_conf_le_ops.write(PCI_HOST_BRIDGE(&s->pci[0]), 0, val, size);
        break;
    case MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG ...
         MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG + 3:
        pci_host_data_le_ops.write(PCI_HOST_BRIDGE(&s->pci[0]),
            addr - MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG, val, size);
        break;
    case MV64340_PCI_1_CONFIG_ADDR:
        pci_host_conf_le_ops.write(PCI_HOST_BRIDGE(&s->pci[1]), 0, val, size);
        break;
    case MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG ...
         MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG + 3:
        pci_host_data_le_ops.write(PCI_HOST_BRIDGE(&s->pci[1]),
            addr - MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG, val, size);
        break;
    case MV64340_CPU_INTERRUPT0_MASK_LOW:
        s->cpu0_int_mask &= 0xffffffff00000000ULL;
        s->cpu0_int_mask |= val & 0xffffffffULL;
        break;
    case MV64340_CPU_INTERRUPT0_MASK_HIGH:
        s->cpu0_int_mask &= 0xffffffffULL;
        s->cpu0_int_mask |= val << 32;
        break;
    case MV64340_CUNIT_ARBITER_CONTROL_REG:
        s->gpp_int_level = !!(val & BIT(10));
        break;
    case MV64340_GPP_IO_CONTROL:
        s->gpp_io = val;
        break;
    case MV64340_GPP_LEVEL_CONTROL:
        s->gpp_level = val;
        break;
    case MV64340_GPP_VALUE:
        s->gpp_value &= ~s->gpp_io;
        s->gpp_value |= val & s->gpp_io;
        break;
    case MV64340_GPP_VALUE_SET:
        s->gpp_value |= val & s->gpp_io;
        break;
    case MV64340_GPP_VALUE_CLEAR:
        s->gpp_value &= ~(val & s->gpp_io);
        break;
    case MV64340_GPP_INTERRUPT_CAUSE:
        if (!s->gpp_int_level && val != s->gpp_int_cr) {
            int i;
            uint32_t ch = s->gpp_int_cr ^ val;
            s->gpp_int_cr = val;
            for (i = 0; i < 4; i++) {
                if ((ch & 0xff << i) && !(val & 0xff << i)) {
                    mv64361_update_irq(opaque, MV64361_IRQ_P0_GPP0_7 + i, 0);
                }
            }
        } else {
            s->gpp_int_cr = val;
        }
        break;
    case MV64340_GPP_INTERRUPT_MASK0:
    case MV64340_GPP_INTERRUPT_MASK1:
        s->gpp_int_mask = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented register write 0x%"
                      HWADDR_PRIx " = %"PRIx64"\n", __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps mv64361_ops = {
    .read = mv64361_read,
    .write = mv64361_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mv64361_gpp_irq(void *opaque, int n, int level)
{
    MV64361State *s = opaque;
    uint32_t mask = BIT(n);
    uint32_t val = s->gpp_value & ~mask;

    if (s->gpp_level & mask) {
        level = !level;
    }
    val |= level << n;
    if (val > s->gpp_value) {
        s->gpp_value = val;
        s->gpp_int_cr |= mask;
        if (s->gpp_int_mask & mask) {
            mv64361_update_irq(opaque, MV64361_IRQ_P0_GPP0_7 + n / 8, 1);
        }
    } else if (val < s->gpp_value) {
        int b = n / 8;
        s->gpp_value = val;
        if (s->gpp_int_level && !(val & 0xff << b)) {
            mv64361_update_irq(opaque, MV64361_IRQ_P0_GPP0_7 + b, 0);
        }
    }
}

static void mv64361_realize(DeviceState *dev, Error **errp)
{
    MV64361State *s = MV64361(dev);
    int i;

    s->base_addr_enable = 0x1fffff;
    memory_region_init_io(&s->regs, OBJECT(s), &mv64361_ops, s,
                          TYPE_MV64361, 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->regs);
    for (i = 0; i < 2; i++) {
        g_autofree char *name = g_strdup_printf("pcihost%d", i);
        object_initialize_child(OBJECT(dev), name, &s->pci[i],
                                TYPE_MV64361_PCI);
        DeviceState *pci = DEVICE(&s->pci[i]);
        qdev_prop_set_uint8(pci, "index", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(pci), &error_fatal);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->cpu_irq);
    qdev_init_gpio_in_named(dev, mv64361_gpp_irq, "gpp", 32);
    /* FIXME: PCI IRQ connections may be board specific */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        s->pci[1].irq[i] = qdev_get_gpio_in_named(dev, "gpp", 12 + i);
    }
}

static void mv64361_reset(DeviceState *dev)
{
    MV64361State *s = MV64361(dev);
    int i, j;

    /*
     * These values may be board specific
     * Real chip supports init from an eprom but that's not modelled
     */
    set_mem_windows(s, 0x1fffff);
    s->cpu_conf = 0x28000ff;
    s->regs_base = 0x100f100;
    s->pci[0].io_base = 0x100f800;
    s->pci[0].io_size = 0xff;
    s->pci[0].mem_base[0] = 0x100c000;
    s->pci[0].mem_size[0] = 0x1fff;
    s->pci[0].mem_base[1] = 0x100f900;
    s->pci[0].mem_size[1] = 0xff;
    s->pci[0].mem_base[2] = 0x100f400;
    s->pci[0].mem_size[2] = 0x1ff;
    s->pci[0].mem_base[3] = 0x100f600;
    s->pci[0].mem_size[3] = 0x1ff;
    s->pci[1].io_base = 0x100fe00;
    s->pci[1].io_size = 0xff;
    s->pci[1].mem_base[0] = 0x1008000;
    s->pci[1].mem_size[0] = 0x3fff;
    s->pci[1].mem_base[1] = 0x100fd00;
    s->pci[1].mem_size[1] = 0xff;
    s->pci[1].mem_base[2] = 0x1002600;
    s->pci[1].mem_size[2] = 0x1ff;
    s->pci[1].mem_base[3] = 0x100ff80;
    s->pci[1].mem_size[3] = 0x7f;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            s->pci[i].remap[j] = s->pci[i].mem_base[j] << 16;
        }
    }
    s->pci[0].remap[1] = 0;
    s->pci[1].remap[1] = 0;
    set_mem_windows(s, 0xfbfff);
}

static void mv64361_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mv64361_realize;
    dc->reset = mv64361_reset;
}

static const TypeInfo mv64361_type_info = {
    .name = TYPE_MV64361,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MV64361State),
    .class_init = mv64361_class_init,
};

static void mv64361_register_types(void)
{
    type_register_static(&mv64361_type_info);
}

type_init(mv64361_register_types)
