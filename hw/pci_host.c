/*
 * pci_host.c
 *
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "pci.h"
#include "pci_host.h"

/* debug PCI */
//#define DEBUG_PCI

#ifdef DEBUG_PCI
#define PCI_DPRINTF(fmt, ...) \
do { printf("pci_host_data: " fmt , ## __VA_ARGS__); } while (0)
#else
#define PCI_DPRINTF(fmt, ...)
#endif

/*
 * PCI address
 * bit 16 - 24: bus number
 * bit  8 - 15: devfun number
 * bit  0 -  7: offset in configuration space of a given pci device
 */

/* the helper functio to get a PCIDeice* for a given pci address */
static inline PCIDevice *pci_addr_to_dev(PCIBus *bus, uint32_t addr)
{
    uint8_t bus_num = (addr >> 16) & 0xff;
    uint8_t devfn = (addr >> 8) & 0xff;
    return pci_find_device(bus, bus_num, PCI_SLOT(devfn), PCI_FUNC(devfn));
}

static inline uint32_t pci_addr_to_config(uint32_t addr)
{
    return addr & (PCI_CONFIG_SPACE_SIZE - 1);
}

void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, int len)
{
    PCIDevice *pci_dev = pci_addr_to_dev(s, addr);
    uint32_t config_addr = pci_addr_to_config(addr);

    if (!pci_dev)
        return;

    PCI_DPRINTF("%s: %s: addr=%02"PRIx32" val=%08"PRI32x" len=%d\n",
                __func__, pci_dev->name, config_addr, val, len);
    pci_dev->config_write(pci_dev, config_addr, val, len);
}

uint32_t pci_data_read(PCIBus *s, uint32_t addr, int len)
{
    PCIDevice *pci_dev = pci_addr_to_dev(s, addr);
    uint32_t config_addr = pci_addr_to_config(addr);
    uint32_t val;

    if (!pci_dev) {
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
    } else {
        val = pci_dev->config_read(pci_dev, config_addr, len);
        PCI_DPRINTF("%s: %s: addr=%02"PRIx32" val=%08"PRIx32" len=%d\n",
                    __func__, pci_dev->name, config_addr, val, len);
    }

    return val;
}

static void pci_host_config_writel(void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    PCIHostState *s = opaque;

#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    PCI_DPRINTF("%s addr " TARGET_FMT_plx " val %"PRIx32"\n",
                __func__, addr, val);
    s->config_reg = val;
}

static uint32_t pci_host_config_readl(void *opaque, target_phys_addr_t addr)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    PCI_DPRINTF("%s addr " TARGET_FMT_plx " val %"PRIx32"\n",
                __func__, addr, val);
    return val;
}

static CPUWriteMemoryFunc * const pci_host_config_write[] = {
    &pci_host_config_writel,
    &pci_host_config_writel,
    &pci_host_config_writel,
};

static CPUReadMemoryFunc * const pci_host_config_read[] = {
    &pci_host_config_readl,
    &pci_host_config_readl,
    &pci_host_config_readl,
};

int pci_host_config_register_io_memory(PCIHostState *s)
{
    return cpu_register_io_memory(pci_host_config_read,
                                  pci_host_config_write, s);
}

static void pci_host_config_writel_noswap(void *opaque,
                                          target_phys_addr_t addr,
                                          uint32_t val)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " val %"PRIx32"\n",
                __func__, addr, val);
    s->config_reg = val;
}

static uint32_t pci_host_config_readl_noswap(void *opaque,
                                             target_phys_addr_t addr)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " val %"PRIx32"\n",
                __func__, addr, val);
    return val;
}

static CPUWriteMemoryFunc * const pci_host_config_write_noswap[] = {
    &pci_host_config_writel_noswap,
    &pci_host_config_writel_noswap,
    &pci_host_config_writel_noswap,
};

static CPUReadMemoryFunc * const pci_host_config_read_noswap[] = {
    &pci_host_config_readl_noswap,
    &pci_host_config_readl_noswap,
    &pci_host_config_readl_noswap,
};

int pci_host_config_register_io_memory_noswap(PCIHostState *s)
{
    return cpu_register_io_memory(pci_host_config_read_noswap,
                                  pci_host_config_write_noswap, s);
}

static void pci_host_config_writel_ioport(void *opaque,
                                          uint32_t addr, uint32_t val)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr %"PRIx32 " val %"PRIx32"\n", __func__, addr, val);
    s->config_reg = val;
}

static uint32_t pci_host_config_readl_ioport(void *opaque, uint32_t addr)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

    PCI_DPRINTF("%s addr %"PRIx32" val %"PRIx32"\n", __func__, addr, val);
    return val;
}

void pci_host_config_register_ioport(pio_addr_t ioport, PCIHostState *s)
{
    register_ioport_write(ioport, 4, 4, pci_host_config_writel_ioport, s);
    register_ioport_read(ioport, 4, 4, pci_host_config_readl_ioport, s);
}

#define PCI_ADDR_T      target_phys_addr_t
#define PCI_HOST_SUFFIX _mmio

#include "pci_host_template.h"

static CPUWriteMemoryFunc * const pci_host_data_write_mmio[] = {
    pci_host_data_writeb_mmio,
    pci_host_data_writew_mmio,
    pci_host_data_writel_mmio,
};

static CPUReadMemoryFunc * const pci_host_data_read_mmio[] = {
    pci_host_data_readb_mmio,
    pci_host_data_readw_mmio,
    pci_host_data_readl_mmio,
};

int pci_host_data_register_io_memory(PCIHostState *s)
{
    return cpu_register_io_memory(pci_host_data_read_mmio,
                                  pci_host_data_write_mmio,
                                  s);
}

#undef PCI_ADDR_T
#undef PCI_HOST_SUFFIX

#define PCI_ADDR_T      uint32_t
#define PCI_HOST_SUFFIX _ioport

#include "pci_host_template.h"

void pci_host_data_register_ioport(pio_addr_t ioport, PCIHostState *s)
{
    register_ioport_write(ioport, 4, 1, pci_host_data_writeb_ioport, s);
    register_ioport_write(ioport, 4, 2, pci_host_data_writew_ioport, s);
    register_ioport_write(ioport, 4, 4, pci_host_data_writel_ioport, s);
    register_ioport_read(ioport, 4, 1, pci_host_data_readb_ioport, s);
    register_ioport_read(ioport, 4, 2, pci_host_data_readw_ioport, s);
    register_ioport_read(ioport, 4, 4, pci_host_data_readl_ioport, s);
}
