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

#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "exec/address-spaces.h"

/* a helper function to get a PCIDevice for a given mmconfig address */
static inline PCIDevice *pcie_dev_find_by_mmcfg_addr(PCIBus *s,
                                                     uint32_t mmcfg_addr)
{
    return pci_find_device(s, PCIE_MMCFG_BUS(mmcfg_addr),
                           PCIE_MMCFG_DEVFN(mmcfg_addr));
}

static void pcie_mmcfg_data_write(void *opaque, hwaddr mmcfg_addr,
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
                                     hwaddr mmcfg_addr,
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

int pcie_host_init(PCIExpressHost *e)
{
    e->base_addr = PCIE_BASE_ADDR_UNMAPPED;

    return 0;
}

void pcie_host_mmcfg_unmap(PCIExpressHost *e)
{
    if (e->base_addr != PCIE_BASE_ADDR_UNMAPPED) {
        memory_region_del_subregion(get_system_memory(), &e->mmio);
        memory_region_destroy(&e->mmio);
        e->base_addr = PCIE_BASE_ADDR_UNMAPPED;
    }
}

void pcie_host_mmcfg_map(PCIExpressHost *e, hwaddr addr,
                         uint32_t size)
{
    assert(!(size & (size - 1)));       /* power of 2 */
    assert(size >= PCIE_MMCFG_SIZE_MIN);
    assert(size <= PCIE_MMCFG_SIZE_MAX);
    e->size = size;
    memory_region_init_io(&e->mmio, OBJECT(e), &pcie_mmcfg_ops, e,
                          "pcie-mmcfg", e->size);
    e->base_addr = addr;
    memory_region_add_subregion(get_system_memory(), e->base_addr, &e->mmio);
}

void pcie_host_mmcfg_update(PCIExpressHost *e,
                            int enable,
                            hwaddr addr,
                            uint32_t size)
{
    pcie_host_mmcfg_unmap(e);
    if (enable) {
        pcie_host_mmcfg_map(e, addr, size);
    }
}

static const TypeInfo pcie_host_type_info = {
    .name = TYPE_PCIE_HOST_BRIDGE,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .abstract = true,
    .instance_size = sizeof(PCIExpressHost),
};

static void pcie_host_register_types(void)
{
    type_register_static(&pcie_host_type_info);
}

type_init(pcie_host_register_types)
