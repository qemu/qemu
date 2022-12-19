/*
 * QMP commands related to PCI
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

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "pci-internal.h"
#include "qapi/qapi-commands-pci.h"

static PciDeviceInfoList *qmp_query_pci_devices(PCIBus *bus, int bus_num);

static PciMemoryRegionList *qmp_query_pci_regions(const PCIDevice *dev)
{
    PciMemoryRegionList *head = NULL, **tail = &head;
    int i;

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        const PCIIORegion *r = &dev->io_regions[i];
        PciMemoryRegion *region;

        if (!r->size) {
            continue;
        }

        region = g_malloc0(sizeof(*region));

        if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
            region->type = g_strdup("io");
        } else {
            region->type = g_strdup("memory");
            region->has_prefetch = true;
            region->prefetch = !!(r->type & PCI_BASE_ADDRESS_MEM_PREFETCH);
            region->has_mem_type_64 = true;
            region->mem_type_64 = !!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64);
        }

        region->bar = i;
        region->address = r->addr;
        region->size = r->size;

        QAPI_LIST_APPEND(tail, region);
    }

    return head;
}

static PciBridgeInfo *qmp_query_pci_bridge(PCIDevice *dev, PCIBus *bus,
                                           int bus_num)
{
    PciBridgeInfo *info;
    PciMemoryRange *range;

    info = g_new0(PciBridgeInfo, 1);

    info->bus = g_new0(PciBusInfo, 1);
    info->bus->number = dev->config[PCI_PRIMARY_BUS];
    info->bus->secondary = dev->config[PCI_SECONDARY_BUS];
    info->bus->subordinate = dev->config[PCI_SUBORDINATE_BUS];

    range = info->bus->io_range = g_new0(PciMemoryRange, 1);
    range->base = pci_bridge_get_base(dev, PCI_BASE_ADDRESS_SPACE_IO);
    range->limit = pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_SPACE_IO);

    range = info->bus->memory_range = g_new0(PciMemoryRange, 1);
    range->base = pci_bridge_get_base(dev, PCI_BASE_ADDRESS_SPACE_MEMORY);
    range->limit = pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_SPACE_MEMORY);

    range = info->bus->prefetchable_range = g_new0(PciMemoryRange, 1);
    range->base = pci_bridge_get_base(dev, PCI_BASE_ADDRESS_MEM_PREFETCH);
    range->limit = pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_MEM_PREFETCH);

    if (dev->config[PCI_SECONDARY_BUS] != 0) {
        PCIBus *child_bus = pci_find_bus_nr(bus,
                                            dev->config[PCI_SECONDARY_BUS]);
        if (child_bus) {
            info->has_devices = true;
            info->devices = qmp_query_pci_devices(child_bus,
                                            dev->config[PCI_SECONDARY_BUS]);
        }
    }

    return info;
}

static PciDeviceInfo *qmp_query_pci_device(PCIDevice *dev, PCIBus *bus,
                                           int bus_num)
{
    const pci_class_desc *desc;
    PciDeviceInfo *info;
    uint8_t type;
    int class;

    info = g_new0(PciDeviceInfo, 1);
    info->bus = bus_num;
    info->slot = PCI_SLOT(dev->devfn);
    info->function = PCI_FUNC(dev->devfn);

    info->class_info = g_new0(PciDeviceClass, 1);
    class = pci_get_word(dev->config + PCI_CLASS_DEVICE);
    info->class_info->q_class = class;
    desc = get_class_desc(class);
    if (desc->desc) {
        info->class_info->desc = g_strdup(desc->desc);
    }

    info->id = g_new0(PciDeviceId, 1);
    info->id->vendor = pci_get_word(dev->config + PCI_VENDOR_ID);
    info->id->device = pci_get_word(dev->config + PCI_DEVICE_ID);
    info->regions = qmp_query_pci_regions(dev);
    info->qdev_id = g_strdup(dev->qdev.id ? dev->qdev.id : "");

    info->irq_pin = dev->config[PCI_INTERRUPT_PIN];
    if (dev->config[PCI_INTERRUPT_PIN] != 0) {
        info->has_irq = true;
        info->irq = dev->config[PCI_INTERRUPT_LINE];
    }

    type = dev->config[PCI_HEADER_TYPE] & ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    if (type == PCI_HEADER_TYPE_BRIDGE) {
        info->pci_bridge = qmp_query_pci_bridge(dev, bus, bus_num);
    } else if (type == PCI_HEADER_TYPE_NORMAL) {
        info->id->has_subsystem = info->id->has_subsystem_vendor = true;
        info->id->subsystem = pci_get_word(dev->config + PCI_SUBSYSTEM_ID);
        info->id->subsystem_vendor =
            pci_get_word(dev->config + PCI_SUBSYSTEM_VENDOR_ID);
    } else if (type == PCI_HEADER_TYPE_CARDBUS) {
        info->id->has_subsystem = info->id->has_subsystem_vendor = true;
        info->id->subsystem = pci_get_word(dev->config + PCI_CB_SUBSYSTEM_ID);
        info->id->subsystem_vendor =
            pci_get_word(dev->config + PCI_CB_SUBSYSTEM_VENDOR_ID);
    }

    return info;
}

static PciDeviceInfoList *qmp_query_pci_devices(PCIBus *bus, int bus_num)
{
    PciDeviceInfoList *head = NULL, **tail = &head;
    PCIDevice *dev;
    int devfn;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        dev = bus->devices[devfn];
        if (dev) {
            QAPI_LIST_APPEND(tail, qmp_query_pci_device(dev, bus, bus_num));
        }
    }

    return head;
}

static PciInfo *qmp_query_pci_bus(PCIBus *bus, int bus_num)
{
    PciInfo *info = NULL;

    bus = pci_find_bus_nr(bus, bus_num);
    if (bus) {
        info = g_malloc0(sizeof(*info));
        info->bus = bus_num;
        info->devices = qmp_query_pci_devices(bus, bus_num);
    }

    return info;
}

PciInfoList *qmp_query_pci(Error **errp)
{
    PciInfoList *head = NULL, **tail = &head;
    PCIHostState *host_bridge;

    QLIST_FOREACH(host_bridge, &pci_host_bridges, next) {
        QAPI_LIST_APPEND(tail,
                         qmp_query_pci_bus(host_bridge->bus,
                                           pci_bus_num(host_bridge->bus)));
    }

    return head;
}
