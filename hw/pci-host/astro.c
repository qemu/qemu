/*
 * HP-PARISC Astro/Pluto/Ike/REO system bus adapter (SBA)
 * with Elroy PCI bus (LBA) adapter emulation
 * Found in C3000 and similar machines
 *
 * (C) 2023 by Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * Chip documentation is available at:
 * https://parisc.wiki.kernel.org/index.php/Technical_Documentation
 *
 * TODO:
 * - All user-added devices are currently attached to the first
 *   Elroy (PCI bus) only for now. To fix this additional work in
 *   SeaBIOS and this driver is needed. See "user_creatable" flag below.
 * - GMMIO (Greater than 4 GB MMIO) register
 */

#define TYPE_ASTRO_IOMMU_MEMORY_REGION "astro-iommu-memory-region"

#define F_EXTEND(addr) ((addr) | MAKE_64BIT_MASK(32, 32))

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/pci-host/astro.h"
#include "hw/hppa/hppa_hardware.h"
#include "migration/vmstate.h"
#include "target/hppa/cpu.h"
#include "trace.h"
#include "qom/object.h"
#include "exec/target_page.h"

/*
 * Helper functions
 */

static uint64_t mask_32bit_val(hwaddr addr, unsigned size, uint64_t val)
{
    if (size == 8) {
        return val;
    }
    if (addr & 4) {
        val >>= 32;
    } else {
        val = (uint32_t) val;
    }
    return val;
}

static void put_val_in_int64(uint64_t *p, hwaddr addr, unsigned size,
                             uint64_t val)
{
    if (size == 8) {
        *p = val;
    } else if (size == 4) {
        if (addr & 4) {
            *p = ((*p << 32) >> 32) | (val << 32);
        } else {
            *p = ((*p >> 32) << 32) | (uint32_t) val;
        }
    }
}

static void put_val_in_arrary(uint64_t *array, hwaddr start_addr,
                              hwaddr addr, unsigned size, uint64_t val)
{
    int index;

    index = (addr - start_addr) / 8;
    put_val_in_int64(&array[index], addr, size, val);
}


/*
 * The Elroy PCI host bridge. We have at least 4 of those under Astro.
 */

static MemTxResult elroy_chip_read_with_attrs(void *opaque, hwaddr addr,
                                             uint64_t *data, unsigned size,
                                             MemTxAttrs attrs)
{
    MemTxResult ret = MEMTX_OK;
    ElroyState *s = opaque;
    uint64_t val = -1;
    int index;

    switch ((addr >> 3) << 3) {
    case 0x0008:
        val = 0x6000005; /* func_class */
        break;
    case 0x0058:
        /*
         * Scratch register, but firmware initializes it with the
         * PCI BUS number and Linux/HP-UX uses it then.
         */
        val = s->pci_bus_num;
        /* Upper byte holds the end of this bus number */
        val |= s->pci_bus_num << 8;
        break;
    case 0x0080:
        val = s->arb_mask; /* set ARB mask */
        break;
    case 0x0108:
        val = s->status_control;
        break;
    case 0x200 ... 0x250 - 1: /* LMMIO, GMMIO, WLMMIO, WGMMIO, ... */
        index = (addr - 0x200) / 8;
        val = s->mmio_base[index];
        break;
    case 0x0680:
        val = s->error_config;
        break;
    case 0x0688:
        val = 0;                /* ERROR_STATUS */
        break;
    case 0x0800:                /* IOSAPIC_REG_SELECT */
        val = s->iosapic_reg_select;
        break;
    case 0x0810:                /* IOSAPIC_REG_WINDOW */
        switch (s->iosapic_reg_select) {
        case 0x01:              /* IOSAPIC_REG_VERSION */
            val = (32 << 16) | 1; /* upper 16bit holds max entries */
            break;
        default:
            if (s->iosapic_reg_select < ARRAY_SIZE(s->iosapic_reg)) {
                val = s->iosapic_reg[s->iosapic_reg_select];
            } else {
                goto check_hf;
            }
        }
        trace_iosapic_reg_read(s->iosapic_reg_select, size, val);
        break;
    default:
    check_hf:
        if (s->status_control & HF_ENABLE) {
            val = 0;
            ret = MEMTX_DECODE_ERROR;
        } else {
            /* return -1ULL if HardFail is disabled */
            val = ~0;
            ret = MEMTX_OK;
        }
    }
    trace_elroy_read(addr, size, val);

    /* for 32-bit accesses mask return value */
    val = mask_32bit_val(addr, size, val);

    trace_astro_chip_read(addr, size, val);
    *data = val;
    return ret;
}


static MemTxResult elroy_chip_write_with_attrs(void *opaque, hwaddr addr,
                                              uint64_t val, unsigned size,
                                              MemTxAttrs attrs)
{
    ElroyState *s = opaque;
    int i;

    trace_elroy_write(addr, size, val);

    switch ((addr >> 3) << 3) {
    case 0x000: /* PCI_ID & PCI_COMMAND_STATUS_REG */
        break;
    case 0x080:
        put_val_in_int64(&s->arb_mask, addr, size, val);
        break;
    case 0x0108:
        put_val_in_int64(&s->status_control, addr, size, val);
        break;
    case 0x200 ... 0x250 - 1:   /* LMMIO, GMMIO, WLMMIO, WGMMIO, ... */
        put_val_in_arrary(s->mmio_base, 0x200, addr, size, val);
        break;
    case 0x300: /* ibase */
    case 0x308: /* imask */
        break;
    case 0x0680:
        put_val_in_int64(&s->error_config, addr, size, val);
        break;
    case 0x0800:                /* IOSAPIC_REG_SELECT */
        s->iosapic_reg_select = val;
        break;
    case 0x0810:                /* IOSAPIC_REG_WINDOW */
        trace_iosapic_reg_write(s->iosapic_reg_select, size, val);
        if (s->iosapic_reg_select < ARRAY_SIZE(s->iosapic_reg)) {
            s->iosapic_reg[s->iosapic_reg_select] = val;
        } else {
            goto check_hf;
        }
        break;
    case 0x0840:                /* IOSAPIC_REG_EOI */
        val = le64_to_cpu(val);
        val &= 63;
        for (i = 0; i < ELROY_IRQS; i++) {
            if ((s->iosapic_reg[0x10 + 2 * i] & 63) == val) {
                s->ilr &= ~(1ull << i);
            }
        }
        break;
    default:
    check_hf:
        if (s->status_control & HF_ENABLE) {
            return MEMTX_DECODE_ERROR;
        }
    }
    return MEMTX_OK;
}

static const MemoryRegionOps elroy_chip_ops = {
    .read_with_attrs = elroy_chip_read_with_attrs,
    .write_with_attrs = elroy_chip_write_with_attrs,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};


/* Unlike pci_config_data_le_ops, no check of high bit set in config_reg.  */

static uint64_t elroy_config_data_read(void *opaque, hwaddr addr, unsigned len)
{
    uint64_t val;

    PCIHostState *s = opaque;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), len);
    trace_elroy_pci_config_data_read(s->config_reg | (addr & 3), len, val);
    return val;
}

static void elroy_config_data_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
    trace_elroy_pci_config_data_write(s->config_reg | (addr & 3), len, val);
}

static const MemoryRegionOps elroy_config_data_ops = {
    .read = elroy_config_data_read,
    .write = elroy_config_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t elroy_config_addr_read(void *opaque, hwaddr addr, unsigned len)
{
    ElroyState *s = opaque;
    return s->config_reg_elroy;
}

static void elroy_config_addr_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    ElroyState *es = opaque;
    es->config_reg_elroy = val; /* keep a copy of original value */
    s->config_reg = val;
}

static const MemoryRegionOps elroy_config_addr_ops = {
    .read = elroy_config_addr_read,
    .write = elroy_config_addr_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


/* Handle PCI-to-system address translation.  */
static IOMMUTLBEntry astro_translate_iommu(IOMMUMemoryRegion *iommu,
                                             hwaddr addr,
                                             IOMMUAccessFlags flag,
                                             int iommu_idx)
{
    AstroState *s = container_of(iommu, AstroState, iommu);
    hwaddr pdir_ptr, index, ibase;
    hwaddr addr_mask = 0xfff; /* 4k translation */
    uint64_t entry;

#define IOVP_SHIFT              12   /* equals PAGE_SHIFT */
#define PDIR_INDEX(iovp)        ((iovp) >> IOVP_SHIFT)
#define SBA_PDIR_VALID_BIT      0x8000000000000000ULL

    addr &= ~addr_mask;

    /*
     * Default translation: "32-bit PCI Addressing on 40-bit Runway".
     * For addresses in the 32-bit memory address range ... and then
     * language which not-coincidentally matches the PSW.W=0 mapping.
     */
    if (addr <= UINT32_MAX) {
        entry = hppa_abs_to_phys_pa2_w0(addr);
    } else {
        entry = addr;
    }

    /* "range enable" flag cleared? */
    if ((s->tlb_ibase & 1) == 0) {
        goto skip;
    }

    ibase = s->tlb_ibase & ~1ULL;
    if ((addr & s->tlb_imask) != ibase) {
        /* do not translate this one! */
        goto skip;
    }

    index = PDIR_INDEX(addr);
    pdir_ptr = s->tlb_pdir_base + index * sizeof(entry);
    entry = ldq_le_phys(&address_space_memory, pdir_ptr);

    if (!(entry & SBA_PDIR_VALID_BIT)) { /* I/O PDIR entry valid ? */
        /* failure */
        return (IOMMUTLBEntry) { .perm = IOMMU_NONE };
    }

    entry &= ~SBA_PDIR_VALID_BIT;
    entry >>= IOVP_SHIFT;
    entry <<= 12;

 skip:
    return (IOMMUTLBEntry) {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = entry,
        .addr_mask = addr_mask,
        .perm = IOMMU_RW,
    };
}

static AddressSpace *elroy_pcihost_set_iommu(PCIBus *bus, void *opaque,
                                            int devfn)
{
    ElroyState *s = opaque;
    return &s->astro->iommu_as;
}

static const PCIIOMMUOps elroy_pcihost_iommu_ops = {
    .get_address_space = elroy_pcihost_set_iommu,
};

/*
 * Encoding in IOSAPIC:
 * base_addr == 0xfffa0000, we want to get 0xa0ff0000.
 * eid  0x0ff00000 -> 0x00ff0000
 * id   0x000ff000 -> 0xff000000
 */
#define SWIZZLE_HPA(a) \
        ((((a) & 0x0ff00000) >> 4) | (((a) & 0x000ff000) << 12))
#define UNSWIZZLE_HPA(a) \
        (((((a) << 4) & 0x0ff00000) | (((a) >> 12) & 0x000ff000) | 0xf0000000))

/* bits in the "low" I/O Sapic IRdT entry */
#define IOSAPIC_IRDT_DISABLE      0x10000 /* if bit is set, mask this irq */
#define IOSAPIC_IRDT_PO_LOW       0x02000
#define IOSAPIC_IRDT_LEVEL_TRIG   0x08000
#define IOSAPIC_IRDT_MODE_LPRI    0x00100

#define CPU_IRQ_OFFSET            2

static void elroy_set_irq(void *opaque, int irq, int level)
{
    ElroyState *s = opaque;
    uint32_t bit;
    uint32_t old_ilr = s->ilr;
    hwaddr cpu_hpa;
    uint32_t val;

    val     = s->iosapic_reg[0x10 + 2 * irq];
    cpu_hpa = s->iosapic_reg[0x11 + 2 * irq];
    /* low nibble of val has value to write into CPU irq reg */
    bit     = 1u << (val & (ELROY_IRQS - 1));
    cpu_hpa = UNSWIZZLE_HPA(cpu_hpa);

    if (level && (!(val & IOSAPIC_IRDT_DISABLE)) && cpu_hpa) {
        uint32_t ena = bit & ~old_ilr;
        s->ilr = old_ilr | bit;
        if (ena != 0) {
            stl_be_phys(&address_space_memory, F_EXTEND(cpu_hpa), val & 63);
        }
    } else {
        s->ilr = old_ilr & ~bit;
    }
}

static int elroy_pci_map_irq(PCIDevice *d, int irq_num)
{
    int slot = PCI_SLOT(d->devfn);

    assert(irq_num >= 0 && irq_num < ELROY_IRQS);
    return slot & (ELROY_IRQS - 1);
}

static void elroy_reset(DeviceState *dev)
{
    ElroyState *s = ELROY_PCI_HOST_BRIDGE(dev);
    int irq;

    /*
     * Make sure to disable interrupts at reboot, otherwise the Linux kernel
     * serial8250_config_port() in drivers/tty/serial/8250/8250_port.c
     * will hang during autoconfig().
     */
    s->ilr = 0;
    for (irq = 0; irq < ELROY_IRQS; irq++) {
        s->iosapic_reg[0x10 + 2 * irq] = IOSAPIC_IRDT_PO_LOW |
                IOSAPIC_IRDT_LEVEL_TRIG | (irq + CPU_IRQ_OFFSET) |
                IOSAPIC_IRDT_DISABLE;
        s->iosapic_reg[0x11 + 2 * irq] = SWIZZLE_HPA(CPU_HPA);
    }
}

static void elroy_pcihost_realize(DeviceState *dev, Error **errp)
{
    ElroyState *s = ELROY_PCI_HOST_BRIDGE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Object *obj = OBJECT(s);

    /* Elroy config access from CPU.  */
    memory_region_init_io(&s->this_mem, obj, &elroy_chip_ops,
                          s, "elroy", 0x2000);

    /* Elroy PCI config. */
    memory_region_init_io(&phb->conf_mem, obj,
                          &elroy_config_addr_ops, dev,
                          "pci-conf-idx", 8);
    memory_region_init_io(&phb->data_mem, obj,
                          &elroy_config_data_ops, dev,
                          "pci-conf-data", 8);
    memory_region_add_subregion(&s->this_mem, 0x40,
                                &phb->conf_mem);
    memory_region_add_subregion(&s->this_mem, 0x48,
                                &phb->data_mem);

    /* Elroy PCI bus memory.  */
    memory_region_init(&s->pci_mmio, obj, "pci-mmio", UINT64_MAX);
    memory_region_init_io(&s->pci_io, obj, &unassigned_io_ops, obj,
                            "pci-isa-mmio",
                            ((uint32_t) IOS_DIST_BASE_SIZE) / ROPES_PER_IOC);

    phb->bus = pci_register_root_bus(DEVICE(s), "pci",
                                     elroy_set_irq, elroy_pci_map_irq, s,
                                     &s->pci_mmio, &s->pci_io,
                                     PCI_DEVFN(0, 0), ELROY_IRQS, TYPE_PCI_BUS);

    sysbus_init_mmio(sbd, &s->this_mem);

    qdev_init_gpio_in(dev, elroy_set_irq, ELROY_IRQS);
}

static const VMStateDescription vmstate_elroy = {
    .name = "Elroy",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(hpa, ElroyState),
        VMSTATE_UINT32(pci_bus_num, ElroyState),
        VMSTATE_UINT64(config_address, ElroyState),
        VMSTATE_UINT64(config_reg_elroy, ElroyState),
        VMSTATE_UINT64(status_control, ElroyState),
        VMSTATE_UINT64(arb_mask, ElroyState),
        VMSTATE_UINT64_ARRAY(mmio_base, ElroyState, (0x0250 - 0x200) / 8),
        VMSTATE_UINT64(error_config, ElroyState),
        VMSTATE_UINT32(iosapic_reg_select, ElroyState),
        VMSTATE_UINT64_ARRAY(iosapic_reg, ElroyState, 0x20),
        VMSTATE_UINT32(ilr, ElroyState),
        VMSTATE_END_OF_LIST()
    }
};

static void elroy_pcihost_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, elroy_reset);
    dc->realize = elroy_pcihost_realize;
    dc->vmsd = &vmstate_elroy;
    dc->user_creatable = false;
}

static const TypeInfo elroy_pcihost_info = {
    .name          = TYPE_ELROY_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(ElroyState),
    .class_init    = elroy_pcihost_class_init,
};

static void elroy_register_types(void)
{
    type_register_static(&elroy_pcihost_info);
}

type_init(elroy_register_types)


static ElroyState *elroy_init(int num)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_ELROY_PCI_HOST_BRIDGE);
    dev->id = g_strdup_printf("elroy%d", num);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return ELROY_PCI_HOST_BRIDGE(dev);
}

/*
 * Astro Runway chip.
 */

static void adjust_LMMIO_DIRECT_mapping(AstroState *s, unsigned int reg_index)
{
    MemoryRegion *lmmio_alias;
    unsigned int lmmio_index, map_route;
    hwaddr map_addr;
    uint32_t map_size;
    struct ElroyState *elroy;

    /* pointer to LMMIO_DIRECT entry */
    lmmio_index = reg_index / 3;
    lmmio_alias = &s->lmmio_direct[lmmio_index];

    map_addr  = s->ioc_ranges[3 * lmmio_index + 0];
    map_size  = s->ioc_ranges[3 * lmmio_index + 1];
    map_route = s->ioc_ranges[3 * lmmio_index + 2];

    /* find elroy to which this address is routed */
    map_route &= (ELROY_NUM - 1);
    elroy = s->elroy[map_route];

    if (lmmio_alias->enabled) {
        memory_region_set_enabled(lmmio_alias, false);
    }

    map_addr = F_EXTEND(map_addr);
    map_addr &= TARGET_PAGE_MASK;
    map_size = (~map_size) + 1;
    map_size &= TARGET_PAGE_MASK;

    /* exit if disabled or zero map size */
    if (!(map_addr & 1) || !map_size) {
        return;
    }

    if (!memory_region_size(lmmio_alias)) {
        memory_region_init_alias(lmmio_alias, OBJECT(elroy),
                        "pci-lmmmio-alias", &elroy->pci_mmio,
                        (uint32_t) map_addr, map_size);
        memory_region_add_subregion(get_system_memory(), map_addr,
                                 lmmio_alias);
    } else {
        memory_region_set_alias_offset(lmmio_alias, map_addr);
        memory_region_set_size(lmmio_alias, map_size);
        memory_region_set_enabled(lmmio_alias, true);
    }
}

static MemTxResult astro_chip_read_with_attrs(void *opaque, hwaddr addr,
                                             uint64_t *data, unsigned size,
                                             MemTxAttrs attrs)
{
    AstroState *s = opaque;
    MemTxResult ret = MEMTX_OK;
    uint64_t val = -1;
    int index;

    switch ((addr >> 3) << 3) {
    /* R2I registers */
    case 0x0000:        /* ID */
        val = (0x01 << 3) | 0x01ULL;
        break;
    case 0x0008:        /* IOC_CTRL */
        val = s->ioc_ctrl;
        break;
    case 0x0010:        /* TOC_CLIENT_ID */
        break;
    case 0x0030:        /* HP-UX 10.20 and 11.11 reads it. No idea. */
        val = -1;
        break;
    case 0x0078:        /* NetBSD reads 0x78 ? */
        val = -1;
        break;
    case 0x0300 ... 0x03d8:     /* LMMIO_DIRECT0_BASE... */
        index = (addr - 0x300) / 8;
        val = s->ioc_ranges[index];
        break;
    case 0x10200:
        val = 0;
        break;
    case 0x10220:
    case 0x10230:        /* HP-UX 11.11 reads it. No idea. */
        val = -1;
        break;
    case 0x22108:        /* IOC STATUS_CONTROL */
        val = s->ioc_status_ctrl;
        break;
    case 0x20200 ... 0x20240 - 1: /* IOC Rope0_Control ... */
        index = (addr - 0x20200) / 8;
        val = s->ioc_rope_control[index];
        break;
    case 0x20040:        /* IOC Rope config */
        val = s->ioc_rope_config;
        break;
    case 0x20050:        /* IOC Rope debug */
        val = 0;
        break;
    case 0x20108:        /* IOC STATUS_CONTROL */
        val = s->ioc_status_control;
        break;
    case 0x20310:        /* IOC_PCOM */
        val = s->tlb_pcom;
        /* TODO: flush iommu */
        break;
    case 0x20400:
        val = s->ioc_flush_control;
        break;
    /* empty placeholders for non-existent elroys */
#define EMPTY_PORT(x) case x:    case x+8:   val = 0;          break; \
                      case x+40: case x+48:  val = UINT64_MAX; break;
        EMPTY_PORT(0x30000)
        EMPTY_PORT(0x32000)
        EMPTY_PORT(0x34000)
        EMPTY_PORT(0x36000)
        EMPTY_PORT(0x38000)
        EMPTY_PORT(0x3a000)
        EMPTY_PORT(0x3c000)
        EMPTY_PORT(0x3e000)
#undef EMPTY_PORT

    default:
        val = 0;
        ret = MEMTX_DECODE_ERROR;
    }

    /* for 32-bit accesses mask return value */
    val = mask_32bit_val(addr, size, val);

    trace_astro_chip_read(addr, size, val);
    *data = val;
    return ret;
}

static MemTxResult astro_chip_write_with_attrs(void *opaque, hwaddr addr,
                                              uint64_t val, unsigned size,
                                              MemTxAttrs attrs)
{
    MemTxResult ret = MEMTX_OK;
    AstroState *s = opaque;

    trace_astro_chip_write(addr, size, val);

    switch ((addr >> 3) << 3) {
    case 0x0000:        /* ID */
        break;
    case 0x0008:        /* IOC_CTRL */
        val &= 0x0ffffff;
        put_val_in_int64(&s->ioc_ctrl, addr, size, val);
        break;
    case 0x0010:        /* TOC_CLIENT_ID */
        break;
    case 0x0030:        /* HP-UX 10.20 and 11.11 reads it. No idea. */
        break;
    case 0x0300 ... 0x03d8 - 1: /* LMMIO_DIRECT0_BASE... */
        put_val_in_arrary(s->ioc_ranges, 0x300, addr, size, val);
        unsigned int index = (addr - 0x300) / 8;
        /* check if one of the 4 LMMIO_DIRECT regs, each using 3 entries. */
        if (index < LMMIO_DIRECT_RANGES * 3) {
            adjust_LMMIO_DIRECT_mapping(s, index);
        }
        break;
    case 0x10200:
    case 0x10220:
    case 0x10230:        /* HP-UX 11.11 reads it. No idea. */
        break;
    case 0x20200 ... 0x20240 - 1: /* IOC Rope0_Control ... */
        put_val_in_arrary(s->ioc_rope_control, 0x20200, addr, size, val);
        break;
    case 0x20040:        /* IOC Rope config */
    case 0x22040:
        put_val_in_int64(&s->ioc_rope_config, addr, size, val);
        break;
    case 0x20300:
    case 0x22300:
        put_val_in_int64(&s->tlb_ibase, addr, size, val);
        break;
    case 0x20308:
    case 0x22308:
        put_val_in_int64(&s->tlb_imask, addr, size, val);
        break;
    case 0x20310:
    case 0x22310:
        put_val_in_int64(&s->tlb_pcom, addr, size, val);
        /* TODO: flush iommu */
        break;
    case 0x20318:
    case 0x22318:
        put_val_in_int64(&s->tlb_tcnfg, addr, size, val);
        break;
    case 0x20320:
    case 0x22320:
        put_val_in_int64(&s->tlb_pdir_base, addr, size, val);
        break;
    case 0x22000:       /* func_id */
        break;
    case 0x22008:       /* func_class */
        break;
    case 0x22050:       /* rope_debug */
        break;
    case 0x22108:        /* IOC STATUS_CONTROL */
        put_val_in_int64(&s->ioc_status_ctrl, addr, size, val);
        break;
    /*
     * empty placeholders for non-existent elroys, e.g.
     * func_class, pci config & data
     */
#define EMPTY_PORT(x) case x: case x+8: case x+0x40: case x+0x48:
        EMPTY_PORT(0x30000)
        EMPTY_PORT(0x32000)
        EMPTY_PORT(0x34000)
        EMPTY_PORT(0x36000)
        EMPTY_PORT(0x38000)
        EMPTY_PORT(0x3a000)
        EMPTY_PORT(0x3c000)
        EMPTY_PORT(0x3e000)
        break;
#undef EMPTY_PORT

    default:
        ret = MEMTX_DECODE_ERROR;
    }
    return ret;
}

static const MemoryRegionOps astro_chip_ops = {
    .read_with_attrs = astro_chip_read_with_attrs,
    .write_with_attrs = astro_chip_write_with_attrs,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static const VMStateDescription vmstate_astro = {
    .name = "Astro",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(ioc_ctrl, AstroState),
        VMSTATE_UINT64(ioc_status_ctrl, AstroState),
        VMSTATE_UINT64_ARRAY(ioc_ranges, AstroState, (0x03d8 - 0x300) / 8),
        VMSTATE_UINT64(ioc_rope_config, AstroState),
        VMSTATE_UINT64(ioc_status_control, AstroState),
        VMSTATE_UINT64(ioc_flush_control, AstroState),
        VMSTATE_UINT64_ARRAY(ioc_rope_control, AstroState, 8),
        VMSTATE_UINT64(tlb_ibase, AstroState),
        VMSTATE_UINT64(tlb_imask, AstroState),
        VMSTATE_UINT64(tlb_pcom, AstroState),
        VMSTATE_UINT64(tlb_tcnfg, AstroState),
        VMSTATE_UINT64(tlb_pdir_base, AstroState),
        VMSTATE_END_OF_LIST()
    }
};

static void astro_reset(DeviceState *dev)
{
    AstroState *s = ASTRO_CHIP(dev);
    int i;

    s->ioc_ctrl = 0x29cf;
    s->ioc_rope_config = 0xc5f;
    s->ioc_flush_control = 0xb03;
    s->ioc_status_control = 0;
    memset(&s->ioc_rope_control, 0, sizeof(s->ioc_rope_control));

    /*
     * The SBA BASE/MASK registers control CPU -> IO routing.
     * The LBA BASE/MASK registers control IO -> System routing (in Elroy)
     */
    memset(&s->ioc_ranges, 0, sizeof(s->ioc_ranges));
    s->ioc_ranges[(0x360 - 0x300) / 8] = LMMIO_DIST_BASE_ADDR | 0x01; /* LMMIO_DIST_BASE (SBA) */
    s->ioc_ranges[(0x368 - 0x300) / 8] = 0xfc000000;          /* LMMIO_DIST_MASK */
    s->ioc_ranges[(0x370 - 0x300) / 8] = 0;                   /* LMMIO_DIST_ROUTE */
    s->ioc_ranges[(0x390 - 0x300) / 8] = IOS_DIST_BASE_ADDR | 0x01; /* IOS_DIST_BASE */
    s->ioc_ranges[(0x398 - 0x300) / 8] = 0xffffff0000;        /* IOS_DIST_MASK    */
    s->ioc_ranges[(0x3a0 - 0x300) / 8] = 0x3400000000000000ULL; /* IOS_DIST_ROUTE */
    s->ioc_ranges[(0x3c0 - 0x300) / 8] = 0xfffee00000;        /* IOS_DIRECT_BASE  */
    s->ioc_ranges[(0x3c8 - 0x300) / 8] = 0xffffff0000;        /* IOS_DIRECT_MASK  */
    s->ioc_ranges[(0x3d0 - 0x300) / 8] = 0x0;                 /* IOS_DIRECT_ROUTE */

    s->tlb_ibase = 0;
    s->tlb_imask = 0;
    s->tlb_pcom = 0;
    s->tlb_tcnfg = 0;
    s->tlb_pdir_base = 0;

    for (i = 0; i < ELROY_NUM; i++) {
        elroy_reset(DEVICE(s->elroy[i]));
    }
}

static void astro_init(Object *obj)
{
}

static void astro_realize(DeviceState *obj, Error **errp)
{
    AstroState *s = ASTRO_CHIP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&s->this_mem, OBJECT(s), &astro_chip_ops,
                          s, "astro", 0x40000);
    sysbus_init_mmio(sbd, &s->this_mem);

    /* Host memory as seen from Elroys PCI side, via the IOMMU.  */
    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_ASTRO_IOMMU_MEMORY_REGION, OBJECT(s),
                             "iommu-astro", UINT64_MAX);
    address_space_init(&s->iommu_as, MEMORY_REGION(&s->iommu),
                       "bm-pci");

    /* Create Elroys (PCI host bus chips).  */
    for (i = 0; i < ELROY_NUM; i++) {
        static const int elroy_hpa_offsets[ELROY_NUM] = {
                    0x30000, 0x32000, 0x38000, 0x3c000 };
        static const char elroy_rope_nr[ELROY_NUM] = {
                    0, 1, 4, 6 }; /* busnum path, e.g. [10:6] */
        int addr_offset;
        ElroyState *elroy;
        hwaddr map_addr;
        uint64_t map_size;
        int rope;

        addr_offset = elroy_hpa_offsets[i];
        rope = elroy_rope_nr[i];

        elroy = elroy_init(i);
        s->elroy[i] = elroy;
        elroy->hpa = ASTRO_HPA + addr_offset;
        elroy->pci_bus_num = i;
        elroy->astro = s;

        /*
         * NOTE: we only allow PCI devices on first Elroy for now.
         * SeaBIOS will not find devices on the other busses.
         */
        if (i > 0) {
            qbus_mark_full(&PCI_HOST_BRIDGE(elroy)->bus->qbus);
        }

        /* map elroy config addresses into Astro space */
        memory_region_add_subregion(&s->this_mem, addr_offset,
                                    &elroy->this_mem);

        /* LMMIO */
        elroy->mmio_base[(0x0200 - 0x200) / 8] = 0xf0000001;
        elroy->mmio_base[(0x0208 - 0x200) / 8] = 0xf8000000;
        /* GMMIO */
        elroy->mmio_base[(0x0210 - 0x200) / 8] = 0x000000f800000001;
        elroy->mmio_base[(0x0218 - 0x200) / 8] = 0x000000ff80000000;
        /* WLMMIO */
        elroy->mmio_base[(0x0220 - 0x200) / 8] = 0xf0000001;
        elroy->mmio_base[(0x0228 - 0x200) / 8] = 0xf0000000;
        /* WGMMIO */
        elroy->mmio_base[(0x0230 - 0x200) / 8] = 0x000000f800000001;
        elroy->mmio_base[(0x0238 - 0x200) / 8] = 0x000000fc00000000;
        /* IOS_BASE */
        map_size = IOS_DIST_BASE_SIZE / ROPES_PER_IOC;
        elroy->mmio_base[(0x0240 - 0x200) / 8] = rope * map_size | 0x01;
        elroy->mmio_base[(0x0248 - 0x200) / 8] = 0x0000e000;

        /* map elroys mmio */
        map_size = LMMIO_DIST_BASE_SIZE / ROPES_PER_IOC;
        map_addr = F_EXTEND(LMMIO_DIST_BASE_ADDR + rope * map_size);
        memory_region_init_alias(&elroy->pci_mmio_alias, OBJECT(elroy),
                                 "pci-mmio-alias",
                                 &elroy->pci_mmio, (uint32_t) map_addr, map_size);
        memory_region_add_subregion(get_system_memory(), map_addr,
                                 &elroy->pci_mmio_alias);

        /* map elroys io */
        map_size = IOS_DIST_BASE_SIZE / ROPES_PER_IOC;
        map_addr = F_EXTEND(IOS_DIST_BASE_ADDR + rope * map_size);
        memory_region_add_subregion(get_system_memory(), map_addr,
                                 &elroy->pci_io);

        /* Host memory as seen from the PCI side, via the IOMMU.  */
        pci_setup_iommu(PCI_HOST_BRIDGE(elroy)->bus, &elroy_pcihost_iommu_ops,
                                 elroy);
    }
}

static void astro_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, astro_reset);
    dc->vmsd = &vmstate_astro;
    dc->realize = astro_realize;
    /*
     * astro with elroys are hard part of the newer PA2.0 machines and can not
     * be created without that hardware
     */
    dc->user_creatable = false;
}

static const TypeInfo astro_chip_info = {
    .name          = TYPE_ASTRO_CHIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = astro_init,
    .instance_size = sizeof(AstroState),
    .class_init    = astro_class_init,
};

static void astro_iommu_memory_region_class_init(ObjectClass *klass,
                                                 const void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = astro_translate_iommu;
}

static const TypeInfo astro_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_ASTRO_IOMMU_MEMORY_REGION,
    .class_init = astro_iommu_memory_region_class_init,
};


static void astro_register_types(void)
{
    type_register_static(&astro_chip_info);
    type_register_static(&astro_iommu_memory_region_info);
}

type_init(astro_register_types)
