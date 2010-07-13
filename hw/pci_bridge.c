/*
 * QEMU PCI bus manager
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to dea

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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM

 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 * split out from pci.c
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 */

#include "pci_bridge.h"
#include "pci_internals.h"

PCIDevice *pci_bridge_get_device(PCIBus *bus)
{
    return bus->parent_dev;
}

static void pci_register_secondary_bus(PCIBus *parent,
                                       PCIBus *bus,
                                       PCIDevice *dev,
                                       pci_map_irq_fn map_irq,
                                       const char *name)
{
    qbus_create_inplace(&bus->qbus, &pci_bus_info, &dev->qdev, name);
    bus->map_irq = map_irq;
    bus->parent_dev = dev;

    QLIST_INIT(&bus->child);
    QLIST_INSERT_HEAD(&parent->child, bus, sibling);
}

static void pci_unregister_secondary_bus(PCIBus *bus)
{
    assert(QLIST_EMPTY(&bus->child));
    QLIST_REMOVE(bus, sibling);
}

static uint32_t pci_config_get_io_base(PCIDevice *d,
                                       uint32_t base, uint32_t base_upper16)
{
    uint32_t val;

    val = ((uint32_t)d->config[base] & PCI_IO_RANGE_MASK) << 8;
    if (d->config[base] & PCI_IO_RANGE_TYPE_32) {
        val |= (uint32_t)pci_get_word(d->config + base_upper16) << 16;
    }
    return val;
}

static pcibus_t pci_config_get_memory_base(PCIDevice *d, uint32_t base)
{
    return ((pcibus_t)pci_get_word(d->config + base) & PCI_MEMORY_RANGE_MASK)
        << 16;
}

static pcibus_t pci_config_get_pref_base(PCIDevice *d,
                                         uint32_t base, uint32_t upper)
{
    pcibus_t tmp;
    pcibus_t val;

    tmp = (pcibus_t)pci_get_word(d->config + base);
    val = (tmp & PCI_PREF_RANGE_MASK) << 16;
    if (tmp & PCI_PREF_RANGE_TYPE_64) {
        val |= (pcibus_t)pci_get_long(d->config + upper) << 32;
    }
    return val;
}

pcibus_t pci_bridge_get_base(PCIDevice *bridge, uint8_t type)
{
    pcibus_t base;
    if (type & PCI_BASE_ADDRESS_SPACE_IO) {
        base = pci_config_get_io_base(bridge,
                                      PCI_IO_BASE, PCI_IO_BASE_UPPER16);
    } else {
        if (type & PCI_BASE_ADDRESS_MEM_PREFETCH) {
            base = pci_config_get_pref_base(
                bridge, PCI_PREF_MEMORY_BASE, PCI_PREF_BASE_UPPER32);
        } else {
            base = pci_config_get_memory_base(bridge, PCI_MEMORY_BASE);
        }
    }

    return base;
}

pcibus_t pci_bridge_get_limit(PCIDevice *bridge, uint8_t type)
{
    pcibus_t limit;
    if (type & PCI_BASE_ADDRESS_SPACE_IO) {
        limit = pci_config_get_io_base(bridge,
                                      PCI_IO_LIMIT, PCI_IO_LIMIT_UPPER16);
        limit |= 0xfff;         /* PCI bridge spec 3.2.5.6. */
    } else {
        if (type & PCI_BASE_ADDRESS_MEM_PREFETCH) {
            limit = pci_config_get_pref_base(
                bridge, PCI_PREF_MEMORY_LIMIT, PCI_PREF_LIMIT_UPPER32);
        } else {
            limit = pci_config_get_memory_base(bridge, PCI_MEMORY_LIMIT);
        }
        limit |= 0xfffff;       /* PCI bridge spec 3.2.5.{1, 8}. */
    }
    return limit;
}

static void pci_bridge_write_config(PCIDevice *d,
                             uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(d, address, val, len);

    if (/* io base/limit */
        ranges_overlap(address, len, PCI_IO_BASE, 2) ||

        /* memory base/limit, prefetchable base/limit and
           io base/limit upper 16 */
        ranges_overlap(address, len, PCI_MEMORY_BASE, 20)) {
        PCIBridge *s = container_of(d, PCIBridge, dev);
        pci_bridge_update_mappings(&s->sec_bus);
    }
}

static int pci_bridge_initfn(PCIDevice *dev)
{
    PCIBridge *s = DO_UPCAST(PCIBridge, dev, dev);

    pci_config_set_vendor_id(s->dev.config, s->vid);
    pci_config_set_device_id(s->dev.config, s->did);

    pci_set_word(dev->config + PCI_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    pci_config_set_class(dev->config, PCI_CLASS_BRIDGE_PCI);
    dev->config[PCI_HEADER_TYPE] =
        (dev->config[PCI_HEADER_TYPE] & PCI_HEADER_TYPE_MULTI_FUNCTION) |
        PCI_HEADER_TYPE_BRIDGE;
    pci_set_word(dev->config + PCI_SEC_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    return 0;
}

static int pci_bridge_exitfn(PCIDevice *pci_dev)
{
    PCIBridge *s = DO_UPCAST(PCIBridge, dev, pci_dev);
    pci_unregister_secondary_bus(&s->sec_bus);
    return 0;
}

PCIBus *pci_bridge_init(PCIBus *bus, int devfn, bool multifunction,
                        uint16_t vid, uint16_t did,
                        pci_map_irq_fn map_irq, const char *name)
{
    PCIDevice *dev;
    PCIBridge *s;

    dev = pci_create_multifunction(bus, devfn, multifunction, "pci-bridge");
    qdev_prop_set_uint32(&dev->qdev, "vendorid", vid);
    qdev_prop_set_uint32(&dev->qdev, "deviceid", did);
    qdev_init_nofail(&dev->qdev);

    s = DO_UPCAST(PCIBridge, dev, dev);
    pci_register_secondary_bus(bus, &s->sec_bus, &s->dev, map_irq, name);
    return &s->sec_bus;
}

static PCIDeviceInfo bridge_info = {
    .qdev.name    = "pci-bridge",
    .qdev.size    = sizeof(PCIBridge),
    .init         = pci_bridge_initfn,
    .exit         = pci_bridge_exitfn,
    .config_write = pci_bridge_write_config,
    .is_bridge    = 1,
    .qdev.props   = (Property[]) {
        DEFINE_PROP_HEX32("vendorid", PCIBridge, vid, 0),
        DEFINE_PROP_HEX32("deviceid", PCIBridge, did, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void pci_register_devices(void)
{
    pci_qdev_register(&bridge_info);
}

device_init(pci_register_devices)
