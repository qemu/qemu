/*
 * QEMU PCI bus manager
 *
 * Copyright (c) 2004 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "vl.h"

//#define DEBUG_PCI

#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in Memory space */
#define PCI_CLASS_DEVICE        0x0a    /* Device class */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

/* just used for simpler irq handling. */
#define PCI_DEVICES_MAX 64
#define PCI_IRQ_WORDS   ((PCI_DEVICES_MAX + 31) / 32)

typedef struct PCIBridge {
    uint32_t config_reg;
    PCIDevice **pci_bus[256];
} PCIBridge;

static PCIBridge pci_bridge;
target_phys_addr_t pci_mem_base;
static int pci_irq_index;
static uint32_t pci_irq_levels[4][PCI_IRQ_WORDS];

/* -1 for devfn means auto assign */
PCIDevice *pci_register_device(const char *name, int instance_size,
                               int bus_num, int devfn,
                               PCIConfigReadFunc *config_read, 
                               PCIConfigWriteFunc *config_write)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice *pci_dev, **bus;

    if (pci_irq_index >= PCI_DEVICES_MAX)
        return NULL;
    
    if (!s->pci_bus[bus_num]) {
        s->pci_bus[bus_num] = qemu_mallocz(256 * sizeof(PCIDevice *));
        if (!s->pci_bus[bus_num])
            return NULL;
    }
    bus = s->pci_bus[bus_num];
    if (devfn < 0) {
        for(devfn = 0 ; devfn < 256; devfn += 8) {
            if (!bus[devfn])
                goto found;
        }
        return NULL;
    found: ;
    }
    pci_dev = qemu_mallocz(instance_size);
    if (!pci_dev)
        return NULL;
    pci_dev->bus_num = bus_num;
    pci_dev->devfn = devfn;
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);

    if (!config_read)
        config_read = pci_default_read_config;
    if (!config_write)
        config_write = pci_default_write_config;
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    pci_dev->irq_index = pci_irq_index++;
    bus[devfn] = pci_dev;
    return pci_dev;
}

void pci_register_io_region(PCIDevice *pci_dev, int region_num, 
                            uint32_t size, int type, 
                            PCIMapIORegionFunc *map_func)
{
    PCIIORegion *r;

    if ((unsigned int)region_num >= PCI_NUM_REGIONS)
        return;
    r = &pci_dev->io_regions[region_num];
    r->addr = -1;
    r->size = size;
    r->type = type;
    r->map_func = map_func;
}

static void pci_addr_writel(void* opaque, uint32_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
    s->config_reg = val;
}

static uint32_t pci_addr_readl(void* opaque, uint32_t addr)
{
    PCIBridge *s = opaque;
    return s->config_reg;
}

static void pci_update_mappings(PCIDevice *d)
{
    PCIIORegion *r;
    int cmd, i;
    uint32_t last_addr, new_addr, config_ofs;
    
    cmd = le16_to_cpu(*(uint16_t *)(d->config + PCI_COMMAND));
    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (i == PCI_ROM_SLOT) {
            config_ofs = 0x30;
        } else {
            config_ofs = 0x10 + i * 4;
        }
        if (r->size != 0) {
            if (r->type & PCI_ADDRESS_SPACE_IO) {
                if (cmd & PCI_COMMAND_IO) {
                    new_addr = le32_to_cpu(*(uint32_t *)(d->config + 
                                                         config_ofs));
                    new_addr = new_addr & ~(r->size - 1);
                    last_addr = new_addr + r->size - 1;
                    /* NOTE: we have only 64K ioports on PC */
                    if (last_addr <= new_addr || new_addr == 0 ||
                        last_addr >= 0x10000) {
                        new_addr = -1;
                    }
                } else {
                    new_addr = -1;
                }
            } else {
                if (cmd & PCI_COMMAND_MEMORY) {
                    new_addr = le32_to_cpu(*(uint32_t *)(d->config + 
                                                         config_ofs));
                    /* the ROM slot has a specific enable bit */
                    if (i == PCI_ROM_SLOT && !(new_addr & 1))
                        goto no_mem_map;
                    new_addr = new_addr & ~(r->size - 1);
                    last_addr = new_addr + r->size - 1;
                    /* NOTE: we do not support wrapping */
                    /* XXX: as we cannot support really dynamic
                       mappings, we handle specific values as invalid
                       mappings. */
                    if (last_addr <= new_addr || new_addr == 0 ||
                        last_addr == -1) {
                        new_addr = -1;
                    }
                } else {
                no_mem_map:
                    new_addr = -1;
                }
            }
            /* now do the real mapping */
            if (new_addr != r->addr) {
                if (r->addr != -1) {
                    if (r->type & PCI_ADDRESS_SPACE_IO) {
                        int class;
                        /* NOTE: specific hack for IDE in PC case:
                           only one byte must be mapped. */
                        class = d->config[0x0a] | (d->config[0x0b] << 8);
                        if (class == 0x0101 && r->size == 4) {
                            isa_unassign_ioport(r->addr + 2, 1);
                        } else {
                            isa_unassign_ioport(r->addr, r->size);
                        }
                    } else {
                        cpu_register_physical_memory(r->addr + pci_mem_base, 
                                                     r->size, 
                                                     IO_MEM_UNASSIGNED);
                    }
                }
                r->addr = new_addr;
                if (r->addr != -1) {
                    r->map_func(d, i, r->addr, r->size, r->type);
                }
            }
        }
    }
}

uint32_t pci_default_read_config(PCIDevice *d, 
                                 uint32_t address, int len)
{
    uint32_t val;
    switch(len) {
    case 1:
        val = d->config[address];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(d->config + address));
        break;
    default:
    case 4:
        val = le32_to_cpu(*(uint32_t *)(d->config + address));
        break;
    }
    return val;
}

void pci_default_write_config(PCIDevice *d, 
                              uint32_t address, uint32_t val, int len)
{
    int can_write, i;
    uint32_t end, addr;

    if (len == 4 && ((address >= 0x10 && address < 0x10 + 4 * 6) || 
                     (address >= 0x30 && address < 0x34))) {
        PCIIORegion *r;
        int reg;

        if ( address >= 0x30 ) {
            reg = PCI_ROM_SLOT;
        }else{
            reg = (address - 0x10) >> 2;
        }
        r = &d->io_regions[reg];
        if (r->size == 0)
            goto default_config;
        /* compute the stored value */
        if (reg == PCI_ROM_SLOT) {
            /* keep ROM enable bit */
            val &= (~(r->size - 1)) | 1;
        } else {
            val &= ~(r->size - 1);
            val |= r->type;
        }
        *(uint32_t *)(d->config + address) = cpu_to_le32(val);
        pci_update_mappings(d);
        return;
    }
 default_config:
    /* not efficient, but simple */
    addr = address;
    for(i = 0; i < len; i++) {
        /* default read/write accesses */
        switch(d->config[0x0e]) {
        case 0x00:
        case 0x80:
            switch(addr) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0e:
            case 0x10 ... 0x27: /* base */
            case 0x30 ... 0x33: /* rom */
            case 0x3d:
                can_write = 0;
                break;
            default:
                can_write = 1;
                break;
            }
            break;
        default:
        case 0x01:
            switch(addr) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0e:
            case 0x38 ... 0x3b: /* rom */
            case 0x3d:
                can_write = 0;
                break;
            default:
                can_write = 1;
                break;
            }
            break;
        }
        if (can_write) {
            d->config[addr] = val;
        }
        addr++;
        val >>= 8;
    }

    end = address + len;
    if (end > PCI_COMMAND && address < (PCI_COMMAND + 2)) {
        /* if the command register is modified, we must modify the mappings */
        pci_update_mappings(d);
    }
}

static void pci_data_write(void *opaque, uint32_t addr, 
                           uint32_t val, int len)
{
    PCIBridge *s = opaque;
    PCIDevice **bus, *pci_dev;
    int config_addr;
    
#if defined(DEBUG_PCI) && 0
    printf("pci_data_write: addr=%08x val=%08x len=%d\n",
           s->config_reg, val, len);
#endif
    if (!(s->config_reg & (1 << 31))) {
        return;
    }
    if ((s->config_reg & 0x3) != 0) {
        return;
    }
    bus = s->pci_bus[(s->config_reg >> 16) & 0xff];
    if (!bus)
        return;
    pci_dev = bus[(s->config_reg >> 8) & 0xff];
    if (!pci_dev)
        return;
    config_addr = (s->config_reg & 0xfc) | (addr & 3);
#if defined(DEBUG_PCI)
    printf("pci_config_write: %s: addr=%02x val=%08x len=%d\n",
           pci_dev->name, config_addr, val, len);
#endif
    pci_dev->config_write(pci_dev, config_addr, val, len);
}

static uint32_t pci_data_read(void *opaque, uint32_t addr, 
                              int len)
{
    PCIBridge *s = opaque;
    PCIDevice **bus, *pci_dev;
    int config_addr;
    uint32_t val;

    if (!(s->config_reg & (1 << 31)))
        goto fail;
    if ((s->config_reg & 0x3) != 0)
        goto fail;
    bus = s->pci_bus[(s->config_reg >> 16) & 0xff];
    if (!bus)
        goto fail;
    pci_dev = bus[(s->config_reg >> 8) & 0xff];
    if (!pci_dev) {
    fail:
        switch(len) {
        case 1:
            val = 0xff;
            break;
        case 2:
            val = 0xffff;
            break;
        default:
        case 4:
            val = 0xffffffff;
            break;
        }
        goto the_end;
    }
    config_addr = (s->config_reg & 0xfc) | (addr & 3);
    val = pci_dev->config_read(pci_dev, config_addr, len);
#if defined(DEBUG_PCI)
    printf("pci_config_read: %s: addr=%02x val=%08x len=%d\n",
           pci_dev->name, config_addr, val, len);
#endif
 the_end:
#if defined(DEBUG_PCI) && 0
    printf("pci_data_read: addr=%08x val=%08x len=%d\n",
           s->config_reg, val, len);
#endif
    return val;
}

static void pci_data_writeb(void* opaque, uint32_t addr, uint32_t val)
{
    pci_data_write(opaque, addr, val, 1);
}

static void pci_data_writew(void* opaque, uint32_t addr, uint32_t val)
{
    pci_data_write(opaque, addr, val, 2);
}

static void pci_data_writel(void* opaque, uint32_t addr, uint32_t val)
{
    pci_data_write(opaque, addr, val, 4);
}

static uint32_t pci_data_readb(void* opaque, uint32_t addr)
{
    return pci_data_read(opaque, addr, 1);
}

static uint32_t pci_data_readw(void* opaque, uint32_t addr)
{
    return pci_data_read(opaque, addr, 2);
}

static uint32_t pci_data_readl(void* opaque, uint32_t addr)
{
    return pci_data_read(opaque, addr, 4);
}

/* i440FX PCI bridge */

void i440fx_init(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice *d;

    register_ioport_write(0xcf8, 4, 4, pci_addr_writel, s);
    register_ioport_read(0xcf8, 4, 4, pci_addr_readl, s);

    register_ioport_write(0xcfc, 4, 1, pci_data_writeb, s);
    register_ioport_write(0xcfc, 4, 2, pci_data_writew, s);
    register_ioport_write(0xcfc, 4, 4, pci_data_writel, s);
    register_ioport_read(0xcfc, 4, 1, pci_data_readb, s);
    register_ioport_read(0xcfc, 4, 2, pci_data_readw, s);
    register_ioport_read(0xcfc, 4, 4, pci_data_readl, s);

    d = pci_register_device("i440FX", sizeof(PCIDevice), 0, 0, 
                            NULL, NULL);

    d->config[0x00] = 0x86; // vendor_id
    d->config[0x01] = 0x80;
    d->config[0x02] = 0x37; // device_id
    d->config[0x03] = 0x12;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x00; // class_sub = host2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x00; // header_type
}

/* PIIX3 PCI to ISA bridge */

typedef struct PIIX3State {
    PCIDevice dev;
} PIIX3State;

PIIX3State *piix3_state;

static void piix3_reset(PIIX3State *d)
{
    uint8_t *pci_conf = d->dev.config;

    pci_conf[0x04] = 0x07; // master, memory and I/O
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;
}

void piix3_init(void)
{
    PIIX3State *d;
    uint8_t *pci_conf;

    d = (PIIX3State *)pci_register_device("PIIX3", sizeof(PIIX3State),
                                          0, -1, 
                                          NULL, NULL);
    piix3_state = d;
    pci_conf = d->dev.config;

    pci_conf[0x00] = 0x86; // Intel
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x00; // 82371SB PIIX3 PCI-to-ISA bridge (Step A1)
    pci_conf[0x03] = 0x70;
    pci_conf[0x0a] = 0x01; // class_sub = PCI_ISA
    pci_conf[0x0b] = 0x06; // class_base = PCI_bridge
    pci_conf[0x0e] = 0x80; // header_type = PCI_multifunction, generic

    piix3_reset(d);
}

/* PREP pci init */

static inline void set_config(PCIBridge *s, target_phys_addr_t addr)
{
    int devfn, i;

    for(i = 0; i < 11; i++) {
        if ((addr & (1 << (11 + i))) != 0)
            break;
    }
    devfn = ((addr >> 8) & 7) | (i << 3);
    s->config_reg = 0x80000000 | (addr & 0xfc) | (devfn << 8);
}

static void PPC_PCIIO_writeb (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
    set_config(s, addr);
    pci_data_write(s, addr, val, 1);
}

static void PPC_PCIIO_writew (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
    set_config(s, addr);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    pci_data_write(s, addr, val, 2);
}

static void PPC_PCIIO_writel (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
    set_config(s, addr);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    pci_data_write(s, addr, val, 4);
}

static uint32_t PPC_PCIIO_readb (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;
    set_config(s, addr);
    val = pci_data_read(s, addr, 1);
    return val;
}

static uint32_t PPC_PCIIO_readw (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;
    set_config(s, addr);
    val = pci_data_read(s, addr, 2);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static uint32_t PPC_PCIIO_readl (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;
    set_config(s, addr);
    val = pci_data_read(s, addr, 4);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

static CPUWriteMemoryFunc *PPC_PCIIO_write[] = {
    &PPC_PCIIO_writeb,
    &PPC_PCIIO_writew,
    &PPC_PCIIO_writel,
};

static CPUReadMemoryFunc *PPC_PCIIO_read[] = {
    &PPC_PCIIO_readb,
    &PPC_PCIIO_readw,
    &PPC_PCIIO_readl,
};

void pci_prep_init(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice *d;
    int PPC_io_memory;

    PPC_io_memory = cpu_register_io_memory(0, PPC_PCIIO_read, 
                                           PPC_PCIIO_write, s);
    cpu_register_physical_memory(0x80800000, 0x00400000, PPC_io_memory);

    d = pci_register_device("PREP PCI Bridge", sizeof(PCIDevice), 0, 0, 
                            NULL, NULL);

    /* XXX: put correct IDs */
    d->config[0x00] = 0x11; // vendor_id
    d->config[0x01] = 0x10;
    d->config[0x02] = 0x26; // device_id
    d->config[0x03] = 0x00;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x04; // class_sub = pci2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x01; // header_type
}


/* pmac pci init */

static void pci_pmac_config_writel (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    s->config_reg = val;
}

static uint32_t pci_pmac_config_readl (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;

    val = s->config_reg;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

static CPUWriteMemoryFunc *pci_pmac_config_write[] = {
    &pci_pmac_config_writel,
    &pci_pmac_config_writel,
    &pci_pmac_config_writel,
};

static CPUReadMemoryFunc *pci_pmac_config_read[] = {
    &pci_pmac_config_readl,
    &pci_pmac_config_readl,
    &pci_pmac_config_readl,
};

static void pci_pmac_writeb (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
    pci_data_write(s, addr, val, 1);
}

static void pci_pmac_writew (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    pci_data_write(s, addr, val, 2);
}

static void pci_pmac_writel (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    pci_data_write(s, addr, val, 4);
}

static uint32_t pci_pmac_readb (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;
    val = pci_data_read(s, addr, 1);
    return val;
}

static uint32_t pci_pmac_readw (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;
    val = pci_data_read(s, addr, 2);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static uint32_t pci_pmac_readl (void *opaque, target_phys_addr_t addr)
{
    PCIBridge *s = opaque;
    uint32_t val;

    val = pci_data_read(s, addr, 4);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

static CPUWriteMemoryFunc *pci_pmac_write[] = {
    &pci_pmac_writeb,
    &pci_pmac_writew,
    &pci_pmac_writel,
};

static CPUReadMemoryFunc *pci_pmac_read[] = {
    &pci_pmac_readb,
    &pci_pmac_readw,
    &pci_pmac_readl,
};

void pci_pmac_init(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice *d;
    int pci_mem_config, pci_mem_data;

    pci_mem_config = cpu_register_io_memory(0, pci_pmac_config_read, 
                                            pci_pmac_config_write, s);
    pci_mem_data = cpu_register_io_memory(0, pci_pmac_read, pci_pmac_write, s);

    cpu_register_physical_memory(0xfec00000, 0x1000, pci_mem_config);
    cpu_register_physical_memory(0xfee00000, 0x1000, pci_mem_data);

    d = pci_register_device("MPC106", sizeof(PCIDevice), 0, 0, 
                            NULL, NULL);

    /* same values as PearPC - check this */
    d->config[0x00] = 0x11; // vendor_id
    d->config[0x01] = 0x10;
    d->config[0x02] = 0x26; // device_id
    d->config[0x03] = 0x00;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x04; // class_sub = pci2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x01; // header_type

    d->config[0x18] = 0x0;  // primary_bus
    d->config[0x19] = 0x1;  // secondary_bus
    d->config[0x1a] = 0x1;  // subordinate_bus
    d->config[0x1c] = 0x10; // io_base
    d->config[0x1d] = 0x20; // io_limit
    
    d->config[0x20] = 0x80; // memory_base
    d->config[0x21] = 0x80;
    d->config[0x22] = 0x90; // memory_limit
    d->config[0x23] = 0x80;
    
    d->config[0x24] = 0x00; // prefetchable_memory_base
    d->config[0x25] = 0x84;
    d->config[0x26] = 0x00; // prefetchable_memory_limit
    d->config[0x27] = 0x85;
}

/***********************************************************/
/* generic PCI irq support */

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static inline int pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    int slot_addend;
    slot_addend = (pci_dev->devfn >> 3);
    return (irq_num + slot_addend) & 3;
}

/* 0 <= irq_num <= 3. level must be 0 or 1 */
#ifdef TARGET_PPC
void pci_set_irq(PCIDevice *pci_dev, int irq_num, int level)
{
}
#else
void pci_set_irq(PCIDevice *pci_dev, int irq_num, int level)
{
    int irq_index, shift, pic_irq, pic_level;
    uint32_t *p;

    irq_num = pci_slot_get_pirq(pci_dev, irq_num);
    irq_index = pci_dev->irq_index;
    p = &pci_irq_levels[irq_num][irq_index >> 5];
    shift = (irq_index & 0x1f);
    *p = (*p & ~(1 << shift)) | (level << shift);

    /* now we change the pic irq level according to the piix irq mappings */
    pic_irq = piix3_state->dev.config[0x60 + irq_num];
    if (pic_irq < 16) {
        /* the pic level is the logical OR of all the PCI irqs mapped
           to it */
        pic_level = 0;
#if (PCI_IRQ_WORDS == 2)
        pic_level = ((pci_irq_levels[irq_num][0] | 
                      pci_irq_levels[irq_num][1]) != 0);
#else
        {
            int i;
            pic_level = 0;
            for(i = 0; i < PCI_IRQ_WORDS; i++) {
                if (pci_irq_levels[irq_num][i]) {
                    pic_level = 1;
                    break;
                }
            }
        }
#endif
        pic_set_irq(pic_irq, pic_level);
    }
}
#endif

/***********************************************************/
/* monitor info on PCI */

static void pci_info_device(PCIDevice *d)
{
    int i, class;
    PCIIORegion *r;

    printf("  Bus %2d, device %3d, function %d:\n",
           d->bus_num, d->devfn >> 3, d->devfn & 7);
    class = le16_to_cpu(*((uint16_t *)(d->config + PCI_CLASS_DEVICE)));
    printf("    ");
    switch(class) {
    case 0x0101:
        printf("IDE controller");
        break;
    case 0x0200:
        printf("Ethernet controller");
        break;
    case 0x0300:
        printf("VGA controller");
        break;
    default:
        printf("Class %04x", class);
        break;
    }
    printf(": PCI device %04x:%04x\n",
           le16_to_cpu(*((uint16_t *)(d->config + PCI_VENDOR_ID))),
           le16_to_cpu(*((uint16_t *)(d->config + PCI_DEVICE_ID))));

    if (d->config[PCI_INTERRUPT_PIN] != 0) {
        printf("      IRQ %d.\n", d->config[PCI_INTERRUPT_LINE]);
    }
    for(i = 0;i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (r->size != 0) {
            printf("      BAR%d: ", i);
            if (r->type & PCI_ADDRESS_SPACE_IO) {
                printf("I/O at 0x%04x [0x%04x].\n", 
                       r->addr, r->addr + r->size - 1);
            } else {
                printf("32 bit memory at 0x%08x [0x%08x].\n", 
                       r->addr, r->addr + r->size - 1);
            }
        }
    }
}

void pci_info(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice **bus;
    int bus_num, devfn;
    
    for(bus_num = 0; bus_num < 256; bus_num++) {
        bus = s->pci_bus[bus_num];
        if (bus) {
            for(devfn = 0; devfn < 256; devfn++) {
                if (bus[devfn])
                    pci_info_device(bus[devfn]);
            }
        }
    }
}

/***********************************************************/
/* XXX: the following should be moved to the PC BIOS */

static uint32_t isa_inb(uint32_t addr)
{
    return cpu_inb(cpu_single_env, addr);
}

static void isa_outb(uint32_t val, uint32_t addr)
{
    cpu_outb(cpu_single_env, addr, val);
}

static uint32_t isa_inw(uint32_t addr)
{
    return cpu_inw(cpu_single_env, addr);
}

static void isa_outw(uint32_t val, uint32_t addr)
{
    cpu_outw(cpu_single_env, addr, val);
}

static uint32_t isa_inl(uint32_t addr)
{
    return cpu_inl(cpu_single_env, addr);
}

static void isa_outl(uint32_t val, uint32_t addr)
{
    cpu_outl(cpu_single_env, addr, val);
}

static void pci_config_writel(PCIDevice *d, uint32_t addr, uint32_t val)
{
    PCIBridge *s = &pci_bridge;
    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | addr;
    pci_data_write(s, 0, val, 4);
}

static void pci_config_writew(PCIDevice *d, uint32_t addr, uint32_t val)
{
    PCIBridge *s = &pci_bridge;
    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | (addr & ~3);
    pci_data_write(s, addr & 3, val, 2);
}

static void pci_config_writeb(PCIDevice *d, uint32_t addr, uint32_t val)
{
    PCIBridge *s = &pci_bridge;
    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | (addr & ~3);
    pci_data_write(s, addr & 3, val, 1);
}

static uint32_t pci_config_readl(PCIDevice *d, uint32_t addr)
{
    PCIBridge *s = &pci_bridge;
    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | addr;
    return pci_data_read(s, 0, 4);
}

static uint32_t pci_config_readw(PCIDevice *d, uint32_t addr)
{
    PCIBridge *s = &pci_bridge;
    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | (addr & ~3);
    return pci_data_read(s, addr & 3, 2);
}

static uint32_t pci_config_readb(PCIDevice *d, uint32_t addr)
{
    PCIBridge *s = &pci_bridge;
    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | (addr & ~3);
    return pci_data_read(s, addr & 3, 1);
}

static uint32_t pci_bios_io_addr;
static uint32_t pci_bios_mem_addr;
/* host irqs corresponding to PCI irqs A-D */
static uint8_t pci_irqs[4] = { 11, 9, 11, 9 };

static void pci_set_io_region_addr(PCIDevice *d, int region_num, uint32_t addr)
{
    PCIIORegion *r;
    uint16_t cmd;
    uint32_t ofs;

    if ( region_num == PCI_ROM_SLOT ) {
        ofs = 0x30;
    }else{
        ofs = 0x10 + region_num * 4;
    }

    pci_config_writel(d, ofs, addr);
    r = &d->io_regions[region_num];

    /* enable memory mappings */
    cmd = pci_config_readw(d, PCI_COMMAND);
    if ( region_num == PCI_ROM_SLOT )
        cmd |= 2;
    else if (r->type & PCI_ADDRESS_SPACE_IO)
        cmd |= 1;
    else
        cmd |= 2;
    pci_config_writew(d, PCI_COMMAND, cmd);
}

static void pci_bios_init_device(PCIDevice *d)
{
    int class;
    PCIIORegion *r;
    uint32_t *paddr;
    int i, pin, pic_irq, vendor_id, device_id;

    class = pci_config_readw(d, PCI_CLASS_DEVICE);
    vendor_id = pci_config_readw(d, PCI_VENDOR_ID);
    device_id = pci_config_readw(d, PCI_DEVICE_ID);
    switch(class) {
    case 0x0101:
        if (vendor_id == 0x8086 && device_id == 0x7010) {
            /* PIIX3 IDE */
            pci_config_writew(d, PCI_COMMAND, PCI_COMMAND_IO);
            pci_config_writew(d, 0x40, 0x8000); // enable IDE0
        } else {
            /* IDE: we map it as in ISA mode */
            pci_set_io_region_addr(d, 0, 0x1f0);
            pci_set_io_region_addr(d, 1, 0x3f4);
            pci_set_io_region_addr(d, 2, 0x170);
            pci_set_io_region_addr(d, 3, 0x374);
        }
        break;
    case 0x0300:
        /* VGA: map frame buffer to default Bochs VBE address */
        pci_set_io_region_addr(d, 0, 0xE0000000);
        break;
    case 0xff00:
        if (vendor_id == 0x0106b && device_id == 0x0017) {
            /* macio bridge */
            pci_set_io_region_addr(d, 0, 0x80800000);
        }
        break;
    default:
        /* default memory mappings */
        for(i = 0; i < PCI_NUM_REGIONS; i++) {
            r = &d->io_regions[i];
            if (r->size) {
                if (r->type & PCI_ADDRESS_SPACE_IO)
                    paddr = &pci_bios_io_addr;
                else
                    paddr = &pci_bios_mem_addr;
                *paddr = (*paddr + r->size - 1) & ~(r->size - 1);
                pci_set_io_region_addr(d, i, *paddr);
                *paddr += r->size;
            }
        }
        break;
    }

    /* map the interrupt */
    pin = pci_config_readb(d, PCI_INTERRUPT_PIN);
    if (pin != 0) {
        pin = pci_slot_get_pirq(d, pin - 1);
        pic_irq = pci_irqs[pin];
        pci_config_writeb(d, PCI_INTERRUPT_LINE, pic_irq);
    }
}

/*
 * This function initializes the PCI devices as a normal PCI BIOS
 * would do. It is provided just in case the BIOS has no support for
 * PCI.
 */
void pci_bios_init(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice **bus;
    int bus_num, devfn, i, irq;
    uint8_t elcr[2];

    pci_bios_io_addr = 0xc000;
    pci_bios_mem_addr = 0xf0000000;

    /* activate IRQ mappings */
    elcr[0] = 0x00;
    elcr[1] = 0x00;
    for(i = 0; i < 4; i++) {
        irq = pci_irqs[i];
        /* set to trigger level */
        elcr[irq >> 3] |= (1 << (irq & 7));
        /* activate irq remapping in PIIX */
        pci_config_writeb((PCIDevice *)piix3_state, 0x60 + i, irq);
    }
    isa_outb(elcr[0], 0x4d0);
    isa_outb(elcr[1], 0x4d1);

    for(bus_num = 0; bus_num < 256; bus_num++) {
        bus = s->pci_bus[bus_num];
        if (bus) {
            for(devfn = 0; devfn < 256; devfn++) {
                if (bus[devfn])
                    pci_bios_init_device(bus[devfn]);
            }
        }
    }
}

/*
 * This function initializes the PCI devices as a normal PCI BIOS
 * would do. It is provided just in case the BIOS has no support for
 * PCI.
 */
void pci_ppc_bios_init(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice **bus;
    int bus_num, devfn, i, irq;
    uint8_t elcr[2];

    pci_bios_io_addr = 0xc000;
    pci_bios_mem_addr = 0xc0000000;

#if 0
    /* activate IRQ mappings */
    elcr[0] = 0x00;
    elcr[1] = 0x00;
    for(i = 0; i < 4; i++) {
        irq = pci_irqs[i];
        /* set to trigger level */
        elcr[irq >> 3] |= (1 << (irq & 7));
        /* activate irq remapping in PIIX */
        pci_config_writeb((PCIDevice *)piix3_state, 0x60 + i, irq);
    }
    isa_outb(elcr[0], 0x4d0);
    isa_outb(elcr[1], 0x4d1);
#endif

    for(bus_num = 0; bus_num < 256; bus_num++) {
        bus = s->pci_bus[bus_num];
        if (bus) {
            for(devfn = 0; devfn < 256; devfn++) {
                if (bus[devfn])
                    pci_bios_init_device(bus[devfn]);
            }
        }
    }
}
