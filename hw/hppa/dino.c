/*
 * HP-PARISC Dino PCI chipset emulation, as in B160L and similiar machines
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
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "hppa_sys.h"
#include "trace.h"
#include "qom/object.h"


#define TYPE_DINO_PCI_HOST_BRIDGE "dino-pcihost"

#define DINO_IAR0               0x004
#define DINO_IODC               0x008
#define DINO_IRR0               0x00C  /* RO */
#define DINO_IAR1               0x010
#define DINO_IRR1               0x014  /* RO */
#define DINO_IMR                0x018
#define DINO_IPR                0x01C
#define DINO_TOC_ADDR           0x020
#define DINO_ICR                0x024
#define DINO_ILR                0x028  /* RO */
#define DINO_IO_COMMAND         0x030  /* WO */
#define DINO_IO_STATUS          0x034  /* RO */
#define DINO_IO_CONTROL         0x038
#define DINO_IO_GSC_ERR_RESP    0x040  /* RO */
#define DINO_IO_ERR_INFO        0x044  /* RO */
#define DINO_IO_PCI_ERR_RESP    0x048  /* RO */
#define DINO_IO_FBB_EN          0x05c
#define DINO_IO_ADDR_EN         0x060
#define DINO_PCI_CONFIG_ADDR    0x064
#define DINO_PCI_CONFIG_DATA    0x068
#define DINO_PCI_IO_DATA        0x06c
#define DINO_PCI_MEM_DATA       0x070  /* Dino 3.x only */
#define DINO_GSC2X_CONFIG       0x7b4  /* RO */
#define DINO_GMASK              0x800
#define DINO_PAMR               0x804
#define DINO_PAPR               0x808
#define DINO_DAMODE             0x80c
#define DINO_PCICMD             0x810
#define DINO_PCISTS             0x814  /* R/WC */
#define DINO_MLTIM              0x81c
#define DINO_BRDG_FEAT          0x820
#define DINO_PCIROR             0x824
#define DINO_PCIWOR             0x828
#define DINO_TLTIM              0x830

#define DINO_IRQS         11      /* bits 0-10 are architected */
#define DINO_IRR_MASK     0x5ff   /* only 10 bits are implemented */
#define DINO_LOCAL_IRQS   (DINO_IRQS + 1)
#define DINO_MASK_IRQ(x)  (1 << (x))

#define PCIINTA   0x001
#define PCIINTB   0x002
#define PCIINTC   0x004
#define PCIINTD   0x008
#define PCIINTE   0x010
#define PCIINTF   0x020
#define GSCEXTINT 0x040
/* #define xxx       0x080 - bit 7 is "default" */
/* #define xxx    0x100 - bit 8 not used */
/* #define xxx    0x200 - bit 9 not used */
#define RS232INT  0x400

#define DINO_MEM_CHUNK_SIZE (8 * MiB)

OBJECT_DECLARE_SIMPLE_TYPE(DinoState, DINO_PCI_HOST_BRIDGE)

#define DINO800_REGS (1 + (DINO_TLTIM - DINO_GMASK) / 4)
static const uint32_t reg800_keep_bits[DINO800_REGS] = {
    MAKE_64BIT_MASK(0, 1),  /* GMASK */
    MAKE_64BIT_MASK(0, 7),  /* PAMR */
    MAKE_64BIT_MASK(0, 7),  /* PAPR */
    MAKE_64BIT_MASK(0, 8),  /* DAMODE */
    MAKE_64BIT_MASK(0, 7),  /* PCICMD */
    MAKE_64BIT_MASK(0, 9),  /* PCISTS */
    MAKE_64BIT_MASK(0, 32), /* Undefined */
    MAKE_64BIT_MASK(0, 8),  /* MLTIM */
    MAKE_64BIT_MASK(0, 30), /* BRDG_FEAT */
    MAKE_64BIT_MASK(0, 24), /* PCIROR */
    MAKE_64BIT_MASK(0, 22), /* PCIWOR */
    MAKE_64BIT_MASK(0, 32), /* Undocumented */
    MAKE_64BIT_MASK(0, 9),  /* TLTIM */
};

struct DinoState {
    PCIHostState parent_obj;

    /* PCI_CONFIG_ADDR is parent_obj.config_reg, via pci_host_conf_be_ops,
       so that we can map PCI_CONFIG_DATA to pci_host_data_be_ops.  */
    uint32_t config_reg_dino; /* keep original copy, including 2 lowest bits */

    uint32_t iar0;
    uint32_t iar1;
    uint32_t imr;
    uint32_t ipr;
    uint32_t icr;
    uint32_t ilr;
    uint32_t io_fbb_en;
    uint32_t io_addr_en;
    uint32_t io_control;
    uint32_t toc_addr;

    uint32_t reg800[DINO800_REGS];

    MemoryRegion this_mem;
    MemoryRegion pci_mem;
    MemoryRegion pci_mem_alias[32];

    AddressSpace bm_as;
    MemoryRegion bm;
    MemoryRegion bm_ram_alias;
    MemoryRegion bm_pci_alias;
    MemoryRegion bm_cpu_alias;
};

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
    MemTxResult ret = MEMTX_OK;
    AddressSpace *io;
    uint16_t ioaddr;
    uint32_t val;

    switch (addr) {
    case DINO_PCI_IO_DATA ... DINO_PCI_IO_DATA + 3:
        /* Read from PCI IO space. */
        io = &address_space_io;
        ioaddr = s->parent_obj.config_reg + (addr & 3);
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
    AddressSpace *io;
    MemTxResult ret;
    uint16_t ioaddr;
    int i;

    trace_dino_chip_write(addr, val);

    switch (addr) {
    case DINO_IO_DATA ... DINO_PCI_IO_DATA + 3:
        /* Write into PCI IO space.  */
        io = &address_space_io;
        ioaddr = s->parent_obj.config_reg + (addr & 3);
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

static void dino_set_timer_irq(void *opaque, int irq, int level)
{
    /* ??? Not connected.  */
}

static void dino_set_serial_irq(void *opaque, int irq, int level)
{
    dino_set_irq(opaque, 10, level);
}

PCIBus *dino_init(MemoryRegion *addr_space,
                  qemu_irq *p_rtc_irq, qemu_irq *p_ser_irq)
{
    DeviceState *dev;
    DinoState *s;
    PCIBus *b;
    int i;

    dev = qdev_new(TYPE_DINO_PCI_HOST_BRIDGE);
    s = DINO_PCI_HOST_BRIDGE(dev);
    s->iar0 = s->iar1 = CPU_HPA + 3;
    s->toc_addr = 0xFFFA0030; /* IO_COMMAND of CPU */

    /* Dino PCI access from main memory.  */
    memory_region_init_io(&s->this_mem, OBJECT(s), &dino_chip_ops,
                          s, "dino", 4096);
    memory_region_add_subregion(addr_space, DINO_HPA, &s->this_mem);

    /* Dino PCI config. */
    memory_region_init_io(&s->parent_obj.conf_mem, OBJECT(&s->parent_obj),
                          &dino_config_addr_ops, dev, "pci-conf-idx", 4);
    memory_region_init_io(&s->parent_obj.data_mem, OBJECT(&s->parent_obj),
                          &dino_config_data_ops, dev, "pci-conf-data", 4);
    memory_region_add_subregion(&s->this_mem, DINO_PCI_CONFIG_ADDR,
                                &s->parent_obj.conf_mem);
    memory_region_add_subregion(&s->this_mem, DINO_CONFIG_DATA,
                                &s->parent_obj.data_mem);

    /* Dino PCI bus memory.  */
    memory_region_init(&s->pci_mem, OBJECT(s), "pci-memory", 4 * GiB);

    b = pci_register_root_bus(dev, "pci", dino_set_irq, dino_pci_map_irq, s,
                              &s->pci_mem, get_system_io(),
                              PCI_DEVFN(0, 0), 32, TYPE_PCI_BUS);
    s->parent_obj.bus = b;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Set up windows into PCI bus memory.  */
    for (i = 1; i < 31; i++) {
        uint32_t addr = 0xf0000000 + i * DINO_MEM_CHUNK_SIZE;
        char *name = g_strdup_printf("PCI Outbound Window %d", i);
        memory_region_init_alias(&s->pci_mem_alias[i], OBJECT(s),
                                 name, &s->pci_mem, addr,
                                 DINO_MEM_CHUNK_SIZE);
        g_free(name);
    }

    /* Set up PCI view of memory: Bus master address space.  */
    memory_region_init(&s->bm, OBJECT(s), "bm-dino", 4 * GiB);
    memory_region_init_alias(&s->bm_ram_alias, OBJECT(s),
                             "bm-system", addr_space, 0,
                             0xf0000000 + DINO_MEM_CHUNK_SIZE);
    memory_region_init_alias(&s->bm_pci_alias, OBJECT(s),
                             "bm-pci", &s->pci_mem,
                             0xf0000000 + DINO_MEM_CHUNK_SIZE,
                             30 * DINO_MEM_CHUNK_SIZE);
    memory_region_init_alias(&s->bm_cpu_alias, OBJECT(s),
                             "bm-cpu", addr_space, 0xfff00000,
                             0xfffff);
    memory_region_add_subregion(&s->bm, 0,
                                &s->bm_ram_alias);
    memory_region_add_subregion(&s->bm,
                                0xf0000000 + DINO_MEM_CHUNK_SIZE,
                                &s->bm_pci_alias);
    memory_region_add_subregion(&s->bm, 0xfff00000,
                                &s->bm_cpu_alias);
    address_space_init(&s->bm_as, &s->bm, "pci-bm");
    pci_setup_iommu(b, dino_pcihost_set_iommu, s);

    *p_rtc_irq = qemu_allocate_irq(dino_set_timer_irq, s, 0);
    *p_ser_irq = qemu_allocate_irq(dino_set_serial_irq, s, 0);

    return b;
}

static void dino_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_dino;
}

static const TypeInfo dino_pcihost_info = {
    .name          = TYPE_DINO_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(DinoState),
    .class_init    = dino_pcihost_class_init,
};

static void dino_register_types(void)
{
    type_register_static(&dino_pcihost_info);
}

type_init(dino_register_types)
