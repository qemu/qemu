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

typedef struct PCIBridge {
    uint32_t config_reg;
    PCIDevice **pci_bus[256];
} PCIBridge;

static PCIBridge pci_bridge;
target_phys_addr_t pci_mem_base;

/* -1 for devfn means auto assign */
PCIDevice *pci_register_device(const char *name, int instance_size,
                               int bus_num, int devfn,
                               PCIConfigReadFunc *config_read, 
                               PCIConfigWriteFunc *config_write)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice *pci_dev, **bus;

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
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    bus[devfn] = pci_dev;
    return pci_dev;
}

void pci_register_io_region(PCIDevice *pci_dev, int region_num, 
                            uint32_t size, int type, 
                            PCIMapIORegionFunc *map_func)
{
    PCIIORegion *r;

    if ((unsigned int)region_num >= 6)
        return;
    r = &pci_dev->io_regions[region_num];
    r->addr = -1;
    r->size = size;
    r->type = type;
    r->map_func = map_func;
}

static void pci_config_writel(void* opaque, uint32_t addr, uint32_t val)
{
    PCIBridge *s = opaque;
    s->config_reg = val;
}

static uint32_t pci_config_readl(void* opaque, uint32_t addr)
{
    PCIBridge *s = opaque;
    return s->config_reg;
}

static void unmap_region(PCIIORegion *r)
{
    if (r->addr == -1)
        return;
#ifdef DEBUG_PCI
    printf("unmap addr=%08x size=%08x\n", r->addr, r->size);
#endif
    if (r->type & PCI_ADDRESS_SPACE_IO) {
        isa_unassign_ioport(r->addr, r->size);
    } else {
        cpu_register_physical_memory(r->addr + pci_mem_base, r->size, 
                                     IO_MEM_UNASSIGNED);
    }
}

static void pci_data_write(void *opaque, uint32_t addr, 
                           uint32_t val, int len)
{
    PCIBridge *s = opaque;
    PCIDevice **bus, *pci_dev;
    int config_addr, reg;
    
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
    if (len == 4 && (config_addr >= 0x10 && config_addr < 0x10 + 4 * 6)) {
        PCIIORegion *r;
        reg = (config_addr - 0x10) >> 2;
        r = &pci_dev->io_regions[reg];
        if (r->size == 0)
            goto default_config;
        if (val != 0xffffffff && val != 0) {
            /* XXX: the memory assignment should be global to handle
               overlaps, but it is not needed at this stage */
            /* first unmap the old region */
            unmap_region(r);
            /* change the address */
            if (r->type & PCI_ADDRESS_SPACE_IO) 
                r->addr = val & ~0x3;
            else
                r->addr = val & ~0xf;
#ifdef DEBUG_PCI
            printf("map addr=%08x size=%08x type=%d\n", 
                   r->addr, r->size, r->type);
#endif
            r->map_func(pci_dev, reg, r->addr, r->size, r->type);
        }
        /* now compute the stored value */
        val &= ~(r->size - 1);
        val |= r->type;
        *(uint32_t *)(pci_dev->config + 0x10 + reg * 4) = cpu_to_le32(val);
    } else {
    default_config:
        pci_dev->config_write(pci_dev, config_addr, val, len);
    }
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
        val = 0;
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

static uint32_t i440_read_config(PCIDevice *d, 
                                 uint32_t address, int len)
{
    uint32_t val;
    val = 0;
    memcpy(&val, d->config + address, len);
    return val;
}

static void i440_write_config(PCIDevice *d, 
                              uint32_t address, uint32_t val, int len)
{
    memcpy(d->config + address, &val, len);
}

void i440fx_init(void)
{
    PCIBridge *s = &pci_bridge;
    PCIDevice *d;

    register_ioport_write(0xcf8, 4, 4, pci_config_writel, s);
    register_ioport_read(0xcf8, 4, 4, pci_config_readl, s);

    register_ioport_write(0xcfc, 4, 1, pci_data_writeb, s);
    register_ioport_write(0xcfc, 4, 2, pci_data_writew, s);
    register_ioport_write(0xcfc, 4, 4, pci_data_writel, s);
    register_ioport_read(0xcfc, 4, 1, pci_data_readb, s);
    register_ioport_read(0xcfc, 4, 2, pci_data_readw, s);
    register_ioport_read(0xcfc, 4, 4, pci_data_readl, s);

    d = pci_register_device("i440FX", sizeof(PCIDevice), 0, 0, 
                            i440_read_config, i440_write_config);

    d->config[0x00] = 0x86; // vendor_id
    d->config[0x01] = 0x80;
    d->config[0x02] = 0x37; // device_id
    d->config[0x03] = 0x12;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x04; // class_sub = pci2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0c] = 0x01; // line_size in 32 bit words
    d->config[0x0e] = 0x01; // header_type
}

/* NOTE: the following should be done by the BIOS */

static uint32_t pci_bios_io_addr;
static uint32_t pci_bios_mem_addr;

static void pci_set_io_region_addr(PCIDevice *d, int region_num, uint32_t addr)
{
    PCIBridge *s = &pci_bridge;
    PCIIORegion *r;

    s->config_reg = 0x80000000 | (d->bus_num << 16) | 
        (d->devfn << 8) | (0x10 + region_num * 4);
    pci_data_write(s, 0, addr, 4);
    r = &d->io_regions[region_num];

    /* enable memory mappings */
    if (r->type & PCI_ADDRESS_SPACE_IO)
        d->config[0x04] |= 1;
    else
        d->config[0x04] |= 2;
}


static void pci_bios_init_device(PCIDevice *d)
{
    int class;
    PCIIORegion *r;
    uint32_t *paddr;
    int i;

    class = d->config[0x0a] | (d->config[0x0b] << 8);
    switch(class) {
    case 0x0101:
        /* IDE: we map it as in ISA mode */
        pci_set_io_region_addr(d, 0, 0x1f0);
        pci_set_io_region_addr(d, 1, 0x3f4);
        pci_set_io_region_addr(d, 2, 0x170);
        pci_set_io_region_addr(d, 3, 0x374);
        break;
    default:
        /* default memory mappings */
        for(i = 0; i < 6; i++) {
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
    int bus_num, devfn;

    pci_bios_io_addr = 0xc000;
    pci_bios_mem_addr = 0xf0000000;

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


