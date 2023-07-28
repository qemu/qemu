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

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "trace.h"

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

/* the helper function to get a PCIDevice* for a given pci address */
static inline PCIDevice *pci_dev_find_by_addr(PCIBus *bus, uint32_t addr)
{
    uint8_t bus_num = addr >> 16;
    uint8_t devfn = addr >> 8;

    return pci_find_device(bus, bus_num, devfn);
}

static void pci_adjust_config_limit(PCIBus *bus, uint32_t *limit)
{
    if ((*limit > PCI_CONFIG_SPACE_SIZE) &&
        !pci_bus_allows_extended_config_space(bus)) {
        *limit = PCI_CONFIG_SPACE_SIZE;
    }
}

static bool is_pci_dev_ejected(PCIDevice *pci_dev)
{
    /*
     * device unplug was requested and the guest acked it,
     * so we stop responding config accesses even if the
     * device is not deleted (failover flow)
     */
    return pci_dev && pci_dev->partially_hotplugged &&
           !pci_dev->qdev.pending_deleted_event;
}

void pci_host_config_write_common(PCIDevice *pci_dev, uint32_t addr,
                                  uint32_t limit, uint32_t val, uint32_t len)
{
    pci_adjust_config_limit(pci_get_bus(pci_dev), &limit);
    if (limit <= addr) {
        return;
    }

    assert(len <= 4);
    /* non-zero functions are only exposed when function 0 is present,
     * allowing direct removal of unexposed functions.
     */
    if ((pci_dev->qdev.hotplugged && !pci_get_function_0(pci_dev)) ||
        !pci_dev->has_power || is_pci_dev_ejected(pci_dev)) {
        return;
    }

    trace_pci_cfg_write(pci_dev->name, pci_dev_bus_num(pci_dev),
                        PCI_SLOT(pci_dev->devfn),
                        PCI_FUNC(pci_dev->devfn), addr, val);
    pci_dev->config_write(pci_dev, addr, val, MIN(len, limit - addr));
}

uint32_t pci_host_config_read_common(PCIDevice *pci_dev, uint32_t addr,
                                     uint32_t limit, uint32_t len)
{
    uint32_t ret;

    pci_adjust_config_limit(pci_get_bus(pci_dev), &limit);
    if (limit <= addr) {
        return ~0x0;
    }

    assert(len <= 4);
    /* non-zero functions are only exposed when function 0 is present,
     * allowing direct removal of unexposed functions.
     */
    if ((pci_dev->qdev.hotplugged && !pci_get_function_0(pci_dev)) ||
        !pci_dev->has_power || is_pci_dev_ejected(pci_dev)) {
        return ~0x0;
    }

    ret = pci_dev->config_read(pci_dev, addr, MIN(len, limit - addr));
    trace_pci_cfg_read(pci_dev->name, pci_dev_bus_num(pci_dev),
                       PCI_SLOT(pci_dev->devfn),
                       PCI_FUNC(pci_dev->devfn), addr, ret);

    return ret;
}

void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, unsigned len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);

    if (!pci_dev) {
        trace_pci_cfg_write("empty", extract32(addr, 16, 8),
                            extract32(addr, 11, 5), extract32(addr, 8, 3),
                            config_addr, val);
        return;
    }

    pci_host_config_write_common(pci_dev, config_addr, PCI_CONFIG_SPACE_SIZE,
                                 val, len);
}

uint32_t pci_data_read(PCIBus *s, uint32_t addr, unsigned len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr);
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);

    if (!pci_dev) {
        trace_pci_cfg_read("empty", extract32(addr, 16, 8),
                           extract32(addr, 11, 5), extract32(addr, 8, 3),
                           config_addr, ~0x0);
        return ~0x0;
    }

    return pci_host_config_read_common(pci_dev, config_addr,
                                       PCI_CONFIG_SPACE_SIZE, len);
}

static void pci_host_config_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr " HWADDR_FMT_plx " len %d val %"PRIx64"\n",
                __func__, addr, len, val);
    if (addr != 0 || len != 4) {
        return;
    }
    s->config_reg = val;
}

static uint64_t pci_host_config_read(void *opaque, hwaddr addr,
                                     unsigned len)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

    PCI_DPRINTF("%s addr " HWADDR_FMT_plx " len %d val %"PRIx32"\n",
                __func__, addr, len, val);
    return val;
}

static void pci_host_data_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    if (s->config_reg & (1u << 31))
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
}

static uint64_t pci_host_data_read(void *opaque,
                                   hwaddr addr, unsigned len)
{
    PCIHostState *s = opaque;

    if (!(s->config_reg & (1U << 31))) {
        return 0xffffffff;
    }
    return pci_data_read(s->bus, s->config_reg | (addr & 3), len);
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

static bool pci_host_needed(void *opaque)
{
    PCIHostState *s = opaque;
    return s->mig_enabled;
}

const VMStateDescription vmstate_pcihost = {
    .name = "PCIHost",
    .needed = pci_host_needed,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(config_reg, PCIHostState),
        VMSTATE_END_OF_LIST()
    }
};

static Property pci_host_properties_common[] = {
    DEFINE_PROP_BOOL("x-config-reg-migration-enabled", PCIHostState,
                     mig_enabled, true),
    DEFINE_PROP_BOOL("bypass-iommu", PCIHostState, bypass_iommu, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void pci_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, pci_host_properties_common);
    dc->vmsd = &vmstate_pcihost;
}

static const TypeInfo pci_host_type_info = {
    .name = TYPE_PCI_HOST_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .abstract = true,
    .class_size = sizeof(PCIHostBridgeClass),
    .instance_size = sizeof(PCIHostState),
    .class_init = pci_host_class_init,
};

static void pci_host_register_types(void)
{
    type_register_static(&pci_host_type_info);
}

type_init(pci_host_register_types)
