/*
 * pcie_host.c
 * utility functions for pci express host bridge.
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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "pci.h"
#include "pcie_host.h"

/*
 * PCI express mmcfig address
 * bit 20 - 28: bus number
 * bit 15 - 19: device number
 * bit 12 - 14: function number
 * bit  0 - 11: offset in configuration space of a given device
 */
#define PCIE_MMCFG_SIZE_MAX             (1ULL << 28)
#define PCIE_MMCFG_SIZE_MIN             (1ULL << 20)
#define PCIE_MMCFG_BUS_BIT              20
#define PCIE_MMCFG_BUS_MASK             0x1ff
#define PCIE_MMCFG_DEVFN_BIT            12
#define PCIE_MMCFG_DEVFN_MASK           0xff
#define PCIE_MMCFG_CONFOFFSET_MASK      0xfff
#define PCIE_MMCFG_BUS(addr)            (((addr) >> PCIE_MMCFG_BUS_BIT) & \
                                         PCIE_MMCFG_BUS_MASK)
#define PCIE_MMCFG_DEVFN(addr)          (((addr) >> PCIE_MMCFG_DEVFN_BIT) & \
                                         PCIE_MMCFG_DEVFN_MASK)
#define PCIE_MMCFG_CONFOFFSET(addr)     ((addr) & PCIE_MMCFG_CONFOFFSET_MASK)


/* a helper function to get a PCIDevice for a given mmconfig address */
static inline PCIDevice *pcie_dev_find_by_mmcfg_addr(PCIBus *s,
                                                     uint32_t mmcfg_addr)
{
    return pci_find_device(s, PCIE_MMCFG_BUS(mmcfg_addr),
                           PCIE_MMCFG_DEVFN(mmcfg_addr));
}

static void pcie_mmcfg_data_write(PCIBus *s,
                                  uint32_t mmcfg_addr, uint32_t val, int len)
{
    PCIDevice *pci_dev = pcie_dev_find_by_mmcfg_addr(s, mmcfg_addr);

    if (!pci_dev)
        return;

    pci_dev->config_write(pci_dev,
                          PCIE_MMCFG_CONFOFFSET(mmcfg_addr), val, len);
}

static uint32_t pcie_mmcfg_data_read(PCIBus *s, uint32_t addr, int len)
{
    PCIDevice *pci_dev = pcie_dev_find_by_mmcfg_addr(s, addr);

    assert(len == 1 || len == 2 || len == 4);
    if (!pci_dev) {
        return ~0x0;
    }
    return pci_dev->config_read(pci_dev, PCIE_MMCFG_CONFOFFSET(addr), len);
}

static void pcie_mmcfg_data_writeb(void *opaque,
                                   target_phys_addr_t addr, uint32_t value)
{
    PCIExpressHost *e = opaque;
    pcie_mmcfg_data_write(e->pci.bus, addr - e->base_addr, value, 1);
}

static void pcie_mmcfg_data_writew(void *opaque,
                                   target_phys_addr_t addr, uint32_t value)
{
    PCIExpressHost *e = opaque;
    pcie_mmcfg_data_write(e->pci.bus, addr - e->base_addr, value, 2);
}

static void pcie_mmcfg_data_writel(void *opaque,
                                   target_phys_addr_t addr, uint32_t value)
{
    PCIExpressHost *e = opaque;
    pcie_mmcfg_data_write(e->pci.bus, addr - e->base_addr, value, 4);
}

static uint32_t pcie_mmcfg_data_readb(void *opaque, target_phys_addr_t addr)
{
    PCIExpressHost *e = opaque;
    return pcie_mmcfg_data_read(e->pci.bus, addr - e->base_addr, 1);
}

static uint32_t pcie_mmcfg_data_readw(void *opaque, target_phys_addr_t addr)
{
    PCIExpressHost *e = opaque;
    return pcie_mmcfg_data_read(e->pci.bus, addr - e->base_addr, 2);
}

static uint32_t pcie_mmcfg_data_readl(void *opaque, target_phys_addr_t addr)
{
    PCIExpressHost *e = opaque;
    return pcie_mmcfg_data_read(e->pci.bus, addr - e->base_addr, 4);
}


static CPUWriteMemoryFunc * const pcie_mmcfg_write[] =
{
    pcie_mmcfg_data_writeb,
    pcie_mmcfg_data_writew,
    pcie_mmcfg_data_writel,
};

static CPUReadMemoryFunc * const pcie_mmcfg_read[] =
{
    pcie_mmcfg_data_readb,
    pcie_mmcfg_data_readw,
    pcie_mmcfg_data_readl,
};

/* pcie_host::base_addr == PCIE_BASE_ADDR_UNMAPPED when it isn't mapped. */
#define PCIE_BASE_ADDR_UNMAPPED  ((target_phys_addr_t)-1ULL)

int pcie_host_init(PCIExpressHost *e)
{
    e->base_addr = PCIE_BASE_ADDR_UNMAPPED;
    e->mmio_index =
        cpu_register_io_memory(pcie_mmcfg_read, pcie_mmcfg_write, e,
                               DEVICE_NATIVE_ENDIAN);
    if (e->mmio_index < 0) {
        return -1;
    }

    return 0;
}

void pcie_host_mmcfg_unmap(PCIExpressHost *e)
{
    if (e->base_addr != PCIE_BASE_ADDR_UNMAPPED) {
        cpu_register_physical_memory(e->base_addr, e->size, IO_MEM_UNASSIGNED);
        e->base_addr = PCIE_BASE_ADDR_UNMAPPED;
    }
}

void pcie_host_mmcfg_map(PCIExpressHost *e,
                         target_phys_addr_t addr, uint32_t size)
{
    assert(!(size & (size - 1)));       /* power of 2 */
    assert(size >= PCIE_MMCFG_SIZE_MIN);
    assert(size <= PCIE_MMCFG_SIZE_MAX);

    e->base_addr = addr;
    e->size = size;
    cpu_register_physical_memory(e->base_addr, e->size, e->mmio_index);
}

void pcie_host_mmcfg_update(PCIExpressHost *e,
                            int enable,
                            target_phys_addr_t addr, uint32_t size)
{
    pcie_host_mmcfg_unmap(e);
    if (enable) {
        pcie_host_mmcfg_map(e, addr, size);
    }
}
