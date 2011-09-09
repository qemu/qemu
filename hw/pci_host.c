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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
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
static inline PCIDevice *pci_dev_find_by_addr(PCIBus *bus, uint32_t addr)
{
    uint8_t bus_num = addr >> 16;
    uint8_t devfn = addr >> 8;

    return pci_find_device(bus, bus_num, devfn);
}

void pci_host_config_write_common(PCIDevice *pci_dev, uint32_t addr,
                                  uint32_t limit, uint32_t val, uint32_t len)
{
    assert(len <= 4);
    pci_dev->config_write(pci_dev, addr, val, MIN(len, limit - addr));
}

uint32_t pci_host_config_read_common(PCIDevice *pci_dev, uint32_t addr,
                                     uint32_t limit, uint32_t len)
{
    assert(len <= 4);
    return pci_dev->config_read(pci_dev, addr, MIN(len, limit - addr));
}

void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, int len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);

    if (!pci_dev) {
        return;
    }

    PCI_DPRINTF("%s: %s: addr=%02" PRIx32 " val=%08" PRIx32 " len=%d\n",
                __func__, pci_dev->name, config_addr, val, len);
    pci_host_config_write_common(pci_dev, config_addr, PCI_CONFIG_SPACE_SIZE,
                                 val, len);
}

uint32_t pci_data_read(PCIBus *s, uint32_t addr, int len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);
    uint32_t val;

    if (!pci_dev) {
        return ~0x0;
    }

    val = pci_host_config_read_common(pci_dev, config_addr,
                                      PCI_CONFIG_SPACE_SIZE, len);
    PCI_DPRINTF("%s: %s: addr=%02"PRIx32" val=%08"PRIx32" len=%d\n",
                __func__, pci_dev->name, config_addr, val, len);

    return val;
}

static void pci_host_config_write(void *opaque, target_phys_addr_t addr,
                                  uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " len %d val %"PRIx64"\n",
                __func__, addr, len, val);
    s->config_reg = val;
}

static uint64_t pci_host_config_read(void *opaque, target_phys_addr_t addr,
                                     unsigned len)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " len %d val %"PRIx32"\n",
                __func__, addr, len, val);
    return val;
}

static void pci_host_data_write(void *opaque, target_phys_addr_t addr,
                                uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;
    PCI_DPRINTF("write addr " TARGET_FMT_plx " len %d val %x\n",
                addr, len, (unsigned)val);
    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
}

static uint64_t pci_host_data_read(void *opaque,
                                   target_phys_addr_t addr, unsigned len)
{
    PCIHostState *s = opaque;
    uint32_t val;
    if (!(s->config_reg & (1 << 31)))
        return 0xffffffff;
    val = pci_data_read(s->bus, s->config_reg | (addr & 3), len);
    PCI_DPRINTF("read addr " TARGET_FMT_plx " len %d val %x\n",
                addr, len, val);
    return val;
}

const MemoryRegionOps pci_host_conf_le_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

const MemoryRegionOps pci_host_conf_be_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

const MemoryRegionOps pci_host_data_le_ops = {
    .read = pci_host_data_read,
    .write = pci_host_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

const MemoryRegionOps pci_host_data_be_ops = {
    .read = pci_host_data_read,
    .write = pci_host_data_write,
    .endianness = DEVICE_BIG_ENDIAN,
};


