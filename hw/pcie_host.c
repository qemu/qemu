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
#include "exec-memory.h"

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

static void pcie_mmcfg_data_write(void *opaque, target_phys_addr_t mmcfg_addr,
                                  uint64_t val, unsigned len)
{
    PCIExpressHost *e = opaque;
    PCIBus *s = e->pci.bus;
    PCIDevice *pci_dev = pcie_dev_find_by_mmcfg_addr(s, mmcfg_addr);
    uint32_t addr;
    uint32_t limit;

    if (!pci_dev) {
        return;
    }
    addr = PCIE_MMCFG_CONFOFFSET(mmcfg_addr);
    limit = pci_config_size(pci_dev);
    if (limit <= addr) {
        /* conventional pci device can be behind pcie-to-pci bridge.
           256 <= addr < 4K has no effects. */
        return;
    }
    pci_host_config_write_common(pci_dev, addr, limit, val, len);
}

static uint64_t pcie_mmcfg_data_read(void *opaque,
                                     target_phys_addr_t mmcfg_addr,
                                     unsigned len)
{
    PCIExpressHost *e = opaque;
    PCIBus *s = e->pci.bus;
    PCIDevice *pci_dev = pcie_dev_find_by_mmcfg_addr(s, mmcfg_addr);
    uint32_t addr;
    uint32_t limit;

    if (!pci_dev) {
        return ~0x0;
    }
    addr = PCIE_MMCFG_CONFOFFSET(mmcfg_addr);
    limit = pci_config_size(pci_dev);
    if (limit <= addr) {
        /* conventional pci device can be behind pcie-to-pci bridge.
           256 <= addr < 4K has no effects. */
        return ~0x0;
    }
    return pci_host_config_read_common(pci_dev, addr, limit, len);
}

static const MemoryRegionOps pcie_mmcfg_ops = {
    .read = pcie_mmcfg_data_read,
    .write = pcie_mmcfg_data_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* pcie_host::base_addr == PCIE_BASE_ADDR_UNMAPPED when it isn't mapped. */
#define PCIE_BASE_ADDR_UNMAPPED  ((target_phys_addr_t)-1ULL)

int pcie_host_init(PCIExpressHost *e, uint32_t size)
{
    assert(!(size & (size - 1)));       /* power of 2 */
    assert(size >= PCIE_MMCFG_SIZE_MIN);
    assert(size <= PCIE_MMCFG_SIZE_MAX);
    e->base_addr = PCIE_BASE_ADDR_UNMAPPED;
    e->size = size;
    memory_region_init_io(&e->mmio, &pcie_mmcfg_ops, e, "pcie-mmcfg", e->size);

    return 0;
}

void pcie_host_mmcfg_unmap(PCIExpressHost *e)
{
    if (e->base_addr != PCIE_BASE_ADDR_UNMAPPED) {
        memory_region_del_subregion(get_system_memory(), &e->mmio);
        e->base_addr = PCIE_BASE_ADDR_UNMAPPED;
    }
}

void pcie_host_mmcfg_map(PCIExpressHost *e, target_phys_addr_t addr)
{
    e->base_addr = addr;
    memory_region_add_subregion(get_system_memory(), e->base_addr, &e->mmio);
}

void pcie_host_mmcfg_update(PCIExpressHost *e,
                            int enable,
                            target_phys_addr_t addr)
{
    pcie_host_mmcfg_unmap(e);
    if (enable) {
        pcie_host_mmcfg_map(e, addr);
    }
}
