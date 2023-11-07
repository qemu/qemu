/*
 * HP-PARISC Dino PCI chipset emulation, as in B160L and similar machines
 *
 * (C) 2017-2019 by Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * Documentation available at:
 * https://parisc.wiki.kernel.org/images-parisc/9/91/Dino_ers.pdf
 * https://parisc.wiki.kernel.org/images-parisc/7/70/Dino_3_1_Errata.pdf
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/dino.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qom/object.h"


/*
 * Dino can forward memory accesses from the CPU in the range between
 * 0xf0800000 and 0xff000000 to the PCI bus.
 */
static void gsc_to_pci_forwarding(DinoState *s)
{
    uint32_t io_addr_en, tmp;
    int enabled, i;

    tmp = extract32(s->io_control, 7, 2);
    enabled = (tmp == 0x01);
    io_addr_en = s->io_addr_en;
    /* Mask out first (=firmware) and last (=Dino) areas. */
    io_addr_en &= ~(BIT(31) | BIT(0));

    memory_region_transaction_begin();
    for (i = 1; i < 31; i++) {
        MemoryRegion *mem = &s->pci_mem_alias[i];
        if (enabled && (io_addr_en & (1U << i))) {
            if (!memory_region_is_mapped(mem)) {
                uint32_t addr = 0xf0000000 + i * DINO_MEM_CHUNK_SIZE;
                memory_region_add_subregion(get_system_memory(), addr, mem);
            }
        } else if (memory_region_is_mapped(mem)) {
            memory_region_del_subregion(get_system_memory(), mem);
        }
    }
    memory_region_transaction_commit();
}

static bool dino_chip_mem_valid(void *opaque, hwaddr addr,
                                unsigned size, bool is_write,
                                MemTxAttrs attrs)
{
    bool ret = false;

    switch (addr) {
    case DINO_IAR0:
    case DINO_IAR1:
    case DINO_IRR0:
    case DINO_IRR1:
    case DINO_IMR:
    case DINO_IPR:
    case DINO_ICR:
    case DINO_ILR:
    case DINO_IO_CONTROL:
    case DINO_IO_FBB_EN:
    case DINO_IO_ADDR_EN:
    case DINO_PCI_IO_DATA:
    case DINO_TOC_ADDR:
    case DINO_GMASK ... DINO_PCISTS:
    case DINO_MLTIM ... DINO_PCIWOR:
    case DINO_TLTIM:
        ret = true;
        break;
    case DINO_PCI_IO_DATA + 2:
        ret = (size <= 2);
        break;
    case DINO_PCI_IO_DATA + 1:
    case DINO_PCI_IO_DATA + 3:
        ret = (size == 1);
    }
    trace_dino_chip_mem_valid(addr, ret);
    return ret;
}

static MemTxResult dino_chip_read_with_attrs(void *opaque, hwaddr addr,
                                             uint64_t *data, unsigned size,
                                             MemTxAttrs attrs)
{
    DinoState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    MemTxResult ret = MEMTX_OK;
    AddressSpace *io;
    uint16_t ioaddr;
    uint32_t val;

    switch (addr) {
    case DINO_PCI_IO_DATA ... DINO_PCI_IO_DATA + 3:
        /* Read from PCI IO space. */
        io = &address_space_io;
        ioaddr = phb->config_reg + (addr & 3);
        switch (size) {
        case 1:
            val = address_space_ldub(io, ioaddr, attrs, &ret);
            break;
        case 2:
            val = address_space_lduw_be(io, ioaddr, attrs, &ret);
            break;
        case 4:
            val = address_space_ldl_be(io, ioaddr, attrs, &ret);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case DINO_IO_FBB_EN:
        val = s->io_fbb_en;
        break;
    case DINO_IO_ADDR_EN:
        val = s->io_addr_en;
        break;
    case DINO_IO_CONTROL:
        val = s->io_control;
        break;

    case DINO_IAR0:
        val = s->iar0;
        break;
    case DINO_IAR1:
        val = s->iar1;
        break;
    case DINO_IMR:
        val = s->imr;
        break;
    case DINO_ICR:
        val = s->icr;
        break;
    case DINO_IPR:
        val = s->ipr;
        /* Any read to IPR clears the register.  */
        s->ipr = 0;
        break;
    case DINO_ILR:
        val = s->ilr;
        break;
    case DINO_IRR0:
        val = s->ilr & s->imr & ~s->icr;
        break;
    case DINO_IRR1:
        val = s->ilr & s->imr & s->icr;
        break;
    case DINO_TOC_ADDR:
        val = s->toc_addr;
        break;
    case DINO_GMASK ... DINO_TLTIM:
        val = s->reg800[(addr - DINO_GMASK) / 4];
        if (addr == DINO_PAMR) {
            val &= ~0x01;  /* LSB is hardwired to 0 */
        }
        if (addr == DINO_MLTIM) {
            val &= ~0x07;  /* 3 LSB are hardwired to 0 */
        }
        if (addr == DINO_BRDG_FEAT) {
            val &= ~(0x10710E0ul | 8); /* bits 5-7, 24 & 15 reserved */
        }
        break;

    default:
        /* Controlled by dino_chip_mem_valid above.  */
        g_assert_not_reached();
    }

    trace_dino_chip_read(addr, val);
    *data = val;
    return ret;
}

static MemTxResult dino_chip_write_with_attrs(void *opaque, hwaddr addr,
                                              uint64_t val, unsigned size,
                                              MemTxAttrs attrs)
{
    DinoState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    AddressSpace *io;
    MemTxResult ret;
    uint16_t ioaddr;
    int i;

    trace_dino_chip_write(addr, val);

    switch (addr) {
    case DINO_IO_DATA ... DINO_PCI_IO_DATA + 3:
        /* Write into PCI IO space.  */
        io = &address_space_io;
        ioaddr = phb->config_reg + (addr & 3);
        switch (size) {
        case 1:
            address_space_stb(io, ioaddr, val, attrs, &ret);
            break;
        case 2:
            address_space_stw_be(io, ioaddr, val, attrs, &ret);
            break;
        case 4:
            address_space_stl_be(io, ioaddr, val, attrs, &ret);
            break;
        default:
            g_assert_not_reached();
        }
        return ret;

    case DINO_IO_FBB_EN:
        s->io_fbb_en = val & 0x03;
        break;
    case DINO_IO_ADDR_EN:
        s->io_addr_en = val;
        gsc_to_pci_forwarding(s);
        break;
    case DINO_IO_CONTROL:
        s->io_control = val;
        gsc_to_pci_forwarding(s);
        break;

    case DINO_IAR0:
        s->iar0 = val;
        break;
    case DINO_IAR1:
        s->iar1 = val;
        break;
    case DINO_IMR:
        s->imr = val;
        break;
    case DINO_ICR:
        s->icr = val;
        break;
    case DINO_IPR:
        /* Any write to IPR clears the register.  */
        s->ipr = 0;
        break;
    case DINO_TOC_ADDR:
        /* IO_COMMAND of CPU with client_id bits */
        s->toc_addr = 0xFFFA0030 | (val & 0x1e000);
        break;

    case DINO_ILR:
    case DINO_IRR0:
    case DINO_IRR1:
        /* These registers are read-only.  */
        break;

    case DINO_GMASK ... DINO_TLTIM:
        i = (addr - DINO_GMASK) / 4;
        val &= reg800_keep_bits[i];
        s->reg800[i] = val;
        break;

    default:
        /* Controlled by dino_chip_mem_valid above.  */
        g_assert_not_reached();
    }
    return MEMTX_OK;
}

static const MemoryRegionOps dino_chip_ops = {
    .read_with_attrs = dino_chip_read_with_attrs,
    .write_with_attrs = dino_chip_write_with_attrs,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .accepts = dino_chip_mem_valid,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_dino = {
    .name = "Dino",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(iar0, DinoState),
        VMSTATE_UINT32(iar1, DinoState),
        VMSTATE_UINT32(imr, DinoState),
        VMSTATE_UINT32(ipr, DinoState),
        VMSTATE_UINT32(icr, DinoState),
        VMSTATE_UINT32(ilr, DinoState),
        VMSTATE_UINT32(io_fbb_en, DinoState),
        VMSTATE_UINT32(io_addr_en, DinoState),
        VMSTATE_UINT32(io_control, DinoState),
        VMSTATE_UINT32(toc_addr, DinoState),
        VMSTATE_END_OF_LIST()
    }
};

/* Unlike pci_config_data_le_ops, no check of high bit set in config_reg.  */

static uint64_t dino_config_data_read(void *opaque, hwaddr addr, unsigned len)
{
    PCIHostState *s = opaque;
    return pci_data_read(s->bus, s->config_reg | (addr & 3), len);
}

static void dino_config_data_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
}

static const MemoryRegionOps dino_config_data_ops = {
    .read = dino_config_data_read,
    .write = dino_config_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t dino_config_addr_read(void *opaque, hwaddr addr, unsigned len)
{
    DinoState *s = opaque;
    return s->config_reg_dino;
}

static void dino_config_addr_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    DinoState *ds = opaque;
    ds->config_reg_dino = val; /* keep a copy of original value */
    s->config_reg = val & ~3U;
}

static const MemoryRegionOps dino_config_addr_ops = {
    .read = dino_config_addr_read,
    .write = dino_config_addr_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static AddressSpace *dino_pcihost_set_iommu(PCIBus *bus, void *opaque,
                                            int devfn)
{
    DinoState *s = opaque;

    return &s->bm_as;
}

static const PCIIOMMUOps dino_iommu_ops = {
    .get_address_space = dino_pcihost_set_iommu,
};

/*
 * Dino interrupts are connected as shown on Page 78, Table 23
 * (Little-endian bit numbers)
 *    0   PCI INTA
 *    1   PCI INTB
 *    2   PCI INTC
 *    3   PCI INTD
 *    4   PCI INTE
 *    5   PCI INTF
 *    6   GSC External Interrupt
 *    7   Bus Error for "less than fatal" mode
 *    8   PS2
 *    9   Unused
 *    10  RS232
 */

static void dino_set_irq(void *opaque, int irq, int level)
{
    DinoState *s = opaque;
    uint32_t bit = 1u << irq;
    uint32_t old_ilr = s->ilr;

    if (level) {
        uint32_t ena = bit & ~old_ilr;
        s->ipr |= ena;
        s->ilr = old_ilr | bit;
        if (ena & s->imr) {
            uint32_t iar = (ena & s->icr ? s->iar1 : s->iar0);
            stl_be_phys(&address_space_memory, iar & -32, iar & 31);
        }
    } else {
        s->ilr = old_ilr & ~bit;
    }
}

static int dino_pci_map_irq(PCIDevice *d, int irq_num)
{
    int slot = PCI_SLOT(d->devfn);

    assert(irq_num >= 0 && irq_num <= 3);

    return slot & 0x03;
}

static void dino_pcihost_reset(DeviceState *dev)
{
    DinoState *s = DINO_PCI_HOST_BRIDGE(dev);

    s->iar0 = s->iar1 = 0xFFFB0000 + 3; /* CPU_HPA + 3 */
    s->toc_addr = 0xFFFA0030; /* IO_COMMAND of CPU */
}

static void dino_pcihost_realize(DeviceState *dev, Error **errp)
{
    DinoState *s = DINO_PCI_HOST_BRIDGE(dev);

    /* Set up PCI view of memory: Bus master address space.  */
    memory_region_init(&s->bm, OBJECT(s), "bm-dino", 4 * GiB);
    memory_region_init_alias(&s->bm_ram_alias, OBJECT(s),
                             "bm-system", s->memory_as, 0,
                             0xf0000000 + DINO_MEM_CHUNK_SIZE);
    memory_region_init_alias(&s->bm_pci_alias, OBJECT(s),
                             "bm-pci", &s->pci_mem,
                             0xf0000000 + DINO_MEM_CHUNK_SIZE,
                             30 * DINO_MEM_CHUNK_SIZE);
    memory_region_init_alias(&s->bm_cpu_alias, OBJECT(s),
                             "bm-cpu", s->memory_as, 0xfff00000,
                             0xfffff);
    memory_region_add_subregion(&s->bm, 0,
                                &s->bm_ram_alias);
    memory_region_add_subregion(&s->bm,
                                0xf0000000 + DINO_MEM_CHUNK_SIZE,
                                &s->bm_pci_alias);
    memory_region_add_subregion(&s->bm, 0xfff00000,
                                &s->bm_cpu_alias);

    address_space_init(&s->bm_as, &s->bm, "pci-bm");
}

static void dino_pcihost_unrealize(DeviceState *dev)
{
    DinoState *s = DINO_PCI_HOST_BRIDGE(dev);

    address_space_destroy(&s->bm_as);
}

static void dino_pcihost_init(Object *obj)
{
    DinoState *s = DINO_PCI_HOST_BRIDGE(obj);
    PCIHostState *phb = PCI_HOST_BRIDGE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    /* Dino PCI access from main memory.  */
    memory_region_init_io(&s->this_mem, OBJECT(s), &dino_chip_ops,
                          s, "dino", 4096);

    /* Dino PCI config. */
    memory_region_init_io(&phb->conf_mem, OBJECT(phb),
                          &dino_config_addr_ops, DEVICE(s),
                          "pci-conf-idx", 4);
    memory_region_init_io(&phb->data_mem, OBJECT(phb),
                          &dino_config_data_ops, DEVICE(s),
                          "pci-conf-data", 4);
    memory_region_add_subregion(&s->this_mem, DINO_PCI_CONFIG_ADDR,
                                &phb->conf_mem);
    memory_region_add_subregion(&s->this_mem, DINO_CONFIG_DATA,
                                &phb->data_mem);

    /* Dino PCI bus memory.  */
    memory_region_init(&s->pci_mem, OBJECT(s), "pci-memory", 4 * GiB);

    phb->bus = pci_register_root_bus(DEVICE(s), "pci",
                                     dino_set_irq, dino_pci_map_irq, s,
                                     &s->pci_mem, get_system_io(),
                                     PCI_DEVFN(0, 0), 32, TYPE_PCI_BUS);

    /* Set up windows into PCI bus memory.  */
    for (i = 1; i < 31; i++) {
        uint32_t addr = 0xf0000000 + i * DINO_MEM_CHUNK_SIZE;
        char *name = g_strdup_printf("PCI Outbound Window %d", i);
        memory_region_init_alias(&s->pci_mem_alias[i], OBJECT(s),
                                 name, &s->pci_mem, addr,
                                 DINO_MEM_CHUNK_SIZE);
        g_free(name);
    }

    pci_setup_iommu(phb->bus, &dino_iommu_ops, s);

    sysbus_init_mmio(sbd, &s->this_mem);

    qdev_init_gpio_in(DEVICE(obj), dino_set_irq, DINO_IRQS);
}

static Property dino_pcihost_properties[] = {
    DEFINE_PROP_LINK("memory-as", DinoState, memory_as, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void dino_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = dino_pcihost_reset;
    dc->realize = dino_pcihost_realize;
    dc->unrealize = dino_pcihost_unrealize;
    device_class_set_props(dc, dino_pcihost_properties);
    dc->vmsd = &vmstate_dino;
}

static const TypeInfo dino_pcihost_info = {
    .name          = TYPE_DINO_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_init = dino_pcihost_init,
    .instance_size = sizeof(DinoState),
    .class_init    = dino_pcihost_class_init,
};

static void dino_register_types(void)
{
    type_register_static(&dino_pcihost_info);
}

type_init(dino_register_types)
