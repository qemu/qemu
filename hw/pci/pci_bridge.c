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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "qapi/error.h"

/* PCI bridge subsystem vendor ID helper functions */
#define PCI_SSVID_SIZEOF        8
#define PCI_SSVID_SVID          4
#define PCI_SSVID_SSID          6

int pci_bridge_ssvid_init(PCIDevice *dev, uint8_t offset,
                          uint16_t svid, uint16_t ssid,
                          Error **errp)
{
    int pos;

    pos = pci_add_capability(dev, PCI_CAP_ID_SSVID, offset,
                             PCI_SSVID_SIZEOF, errp);
    if (pos < 0) {
        return pos;
    }

    pci_set_word(dev->config + pos + PCI_SSVID_SVID, svid);
    pci_set_word(dev->config + pos + PCI_SSVID_SSID, ssid);
    return pos;
}

/* Accessor function to get parent bridge device from pci bus. */
PCIDevice *pci_bridge_get_device(PCIBus *bus)
{
    return bus->parent_dev;
}

/* Accessor function to get secondary bus from pci-to-pci bridge device */
PCIBus *pci_bridge_get_sec_bus(PCIBridge *br)
{
    return &br->sec_bus;
}

static uint32_t pci_config_get_io_base(const PCIDevice *d,
                                       uint32_t base, uint32_t base_upper16)
{
    uint32_t val;

    val = ((uint32_t)d->config[base] & PCI_IO_RANGE_MASK) << 8;
    if (d->config[base] & PCI_IO_RANGE_TYPE_32) {
        val |= (uint32_t)pci_get_word(d->config + base_upper16) << 16;
    }
    return val;
}

static pcibus_t pci_config_get_memory_base(const PCIDevice *d, uint32_t base)
{
    return ((pcibus_t)pci_get_word(d->config + base) & PCI_MEMORY_RANGE_MASK)
        << 16;
}

static pcibus_t pci_config_get_pref_base(const PCIDevice *d,
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

/* accessor function to get bridge filtering base address */
pcibus_t pci_bridge_get_base(const PCIDevice *bridge, uint8_t type)
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

/* accessor function to get bridge filtering limit */
pcibus_t pci_bridge_get_limit(const PCIDevice *bridge, uint8_t type)
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

static void pci_bridge_init_alias(PCIBridge *bridge, MemoryRegion *alias,
                                  uint8_t type, const char *name,
                                  MemoryRegion *space,
                                  MemoryRegion *parent_space,
                                  bool enabled)
{
    PCIDevice *bridge_dev = PCI_DEVICE(bridge);
    pcibus_t base = pci_bridge_get_base(bridge_dev, type);
    pcibus_t limit = pci_bridge_get_limit(bridge_dev, type);
    /* TODO: this doesn't handle base = 0 limit = 2^64 - 1 correctly.
     * Apparently no way to do this with existing memory APIs. */
    pcibus_t size = enabled && limit >= base ? limit + 1 - base : 0;

    memory_region_init_alias(alias, OBJECT(bridge), name, space, base, size);
    memory_region_add_subregion_overlap(parent_space, base, alias, 1);
}

static void pci_bridge_init_vga_aliases(PCIBridge *br, PCIBus *parent,
                                        MemoryRegion *alias_vga)
{
    PCIDevice *pd = PCI_DEVICE(br);
    uint16_t brctl = pci_get_word(pd->config + PCI_BRIDGE_CONTROL);

    memory_region_init_alias(&alias_vga[QEMU_PCI_VGA_IO_LO], OBJECT(br),
                             "pci_bridge_vga_io_lo", &br->address_space_io,
                             QEMU_PCI_VGA_IO_LO_BASE, QEMU_PCI_VGA_IO_LO_SIZE);
    memory_region_init_alias(&alias_vga[QEMU_PCI_VGA_IO_HI], OBJECT(br),
                             "pci_bridge_vga_io_hi", &br->address_space_io,
                             QEMU_PCI_VGA_IO_HI_BASE, QEMU_PCI_VGA_IO_HI_SIZE);
    memory_region_init_alias(&alias_vga[QEMU_PCI_VGA_MEM], OBJECT(br),
                             "pci_bridge_vga_mem", &br->address_space_mem,
                             QEMU_PCI_VGA_MEM_BASE, QEMU_PCI_VGA_MEM_SIZE);

    if (brctl & PCI_BRIDGE_CTL_VGA) {
        pci_register_vga(pd, &alias_vga[QEMU_PCI_VGA_MEM],
                         &alias_vga[QEMU_PCI_VGA_IO_LO],
                         &alias_vga[QEMU_PCI_VGA_IO_HI]);
    }
}

static PCIBridgeWindows *pci_bridge_region_init(PCIBridge *br)
{
    PCIDevice *pd = PCI_DEVICE(br);
    PCIBus *parent = pci_get_bus(pd);
    PCIBridgeWindows *w = g_new(PCIBridgeWindows, 1);
    uint16_t cmd = pci_get_word(pd->config + PCI_COMMAND);

    pci_bridge_init_alias(br, &w->alias_pref_mem,
                          PCI_BASE_ADDRESS_MEM_PREFETCH,
                          "pci_bridge_pref_mem",
                          &br->address_space_mem,
                          parent->address_space_mem,
                          cmd & PCI_COMMAND_MEMORY);
    pci_bridge_init_alias(br, &w->alias_mem,
                          PCI_BASE_ADDRESS_SPACE_MEMORY,
                          "pci_bridge_mem",
                          &br->address_space_mem,
                          parent->address_space_mem,
                          cmd & PCI_COMMAND_MEMORY);
    pci_bridge_init_alias(br, &w->alias_io,
                          PCI_BASE_ADDRESS_SPACE_IO,
                          "pci_bridge_io",
                          &br->address_space_io,
                          parent->address_space_io,
                          cmd & PCI_COMMAND_IO);

    pci_bridge_init_vga_aliases(br, parent, w->alias_vga);

    return w;
}

static void pci_bridge_region_del(PCIBridge *br, PCIBridgeWindows *w)
{
    PCIDevice *pd = PCI_DEVICE(br);
    PCIBus *parent = pci_get_bus(pd);

    memory_region_del_subregion(parent->address_space_io, &w->alias_io);
    memory_region_del_subregion(parent->address_space_mem, &w->alias_mem);
    memory_region_del_subregion(parent->address_space_mem, &w->alias_pref_mem);
    pci_unregister_vga(pd);
}

static void pci_bridge_region_cleanup(PCIBridge *br, PCIBridgeWindows *w)
{
    object_unparent(OBJECT(&w->alias_io));
    object_unparent(OBJECT(&w->alias_mem));
    object_unparent(OBJECT(&w->alias_pref_mem));
    object_unparent(OBJECT(&w->alias_vga[QEMU_PCI_VGA_IO_LO]));
    object_unparent(OBJECT(&w->alias_vga[QEMU_PCI_VGA_IO_HI]));
    object_unparent(OBJECT(&w->alias_vga[QEMU_PCI_VGA_MEM]));
    g_free(w);
}

void pci_bridge_update_mappings(PCIBridge *br)
{
    PCIBridgeWindows *w = br->windows;

    /* Make updates atomic to: handle the case of one VCPU updating the bridge
     * while another accesses an unaffected region. */
    memory_region_transaction_begin();
    pci_bridge_region_del(br, br->windows);
    pci_bridge_region_cleanup(br, w);
    br->windows = pci_bridge_region_init(br);
    memory_region_transaction_commit();
}

/* default write_config function for PCI-to-PCI bridge */
void pci_bridge_write_config(PCIDevice *d,
                             uint32_t address, uint32_t val, int len)
{
    PCIBridge *s = PCI_BRIDGE(d);
    uint16_t oldctl = pci_get_word(d->config + PCI_BRIDGE_CONTROL);
    uint16_t newctl;

    pci_default_write_config(d, address, val, len);

    if (ranges_overlap(address, len, PCI_COMMAND, 2) ||

        /* io base/limit */
        ranges_overlap(address, len, PCI_IO_BASE, 2) ||

        /* memory base/limit, prefetchable base/limit and
           io base/limit upper 16 */
        ranges_overlap(address, len, PCI_MEMORY_BASE, 20) ||

        /* vga enable */
        ranges_overlap(address, len, PCI_BRIDGE_CONTROL, 2)) {
        pci_bridge_update_mappings(s);
    }

    newctl = pci_get_word(d->config + PCI_BRIDGE_CONTROL);
    if (~oldctl & newctl & PCI_BRIDGE_CTL_BUS_RESET) {
        /* Trigger hot reset on 0->1 transition. */
        qbus_reset_all(BUS(&s->sec_bus));
    }
}

void pci_bridge_disable_base_limit(PCIDevice *dev)
{
    uint8_t *conf = dev->config;

    pci_byte_test_and_set_mask(conf + PCI_IO_BASE,
                               PCI_IO_RANGE_MASK & 0xff);
    pci_byte_test_and_clear_mask(conf + PCI_IO_LIMIT,
                                 PCI_IO_RANGE_MASK & 0xff);
    pci_word_test_and_set_mask(conf + PCI_MEMORY_BASE,
                               PCI_MEMORY_RANGE_MASK & 0xffff);
    pci_word_test_and_clear_mask(conf + PCI_MEMORY_LIMIT,
                                 PCI_MEMORY_RANGE_MASK & 0xffff);
    pci_word_test_and_set_mask(conf + PCI_PREF_MEMORY_BASE,
                               PCI_PREF_RANGE_MASK & 0xffff);
    pci_word_test_and_clear_mask(conf + PCI_PREF_MEMORY_LIMIT,
                                 PCI_PREF_RANGE_MASK & 0xffff);
    pci_set_long(conf + PCI_PREF_BASE_UPPER32, 0);
    pci_set_long(conf + PCI_PREF_LIMIT_UPPER32, 0);
}

/* reset bridge specific configuration registers */
void pci_bridge_reset(DeviceState *qdev)
{
    PCIDevice *dev = PCI_DEVICE(qdev);
    uint8_t *conf = dev->config;

    conf[PCI_PRIMARY_BUS] = 0;
    conf[PCI_SECONDARY_BUS] = 0;
    conf[PCI_SUBORDINATE_BUS] = 0;
    conf[PCI_SEC_LATENCY_TIMER] = 0;

    /*
     * the default values for base/limit registers aren't specified
     * in the PCI-to-PCI-bridge spec. So we don't touch them here.
     * Each implementation can override it.
     * typical implementation does
     * zero base/limit registers or
     * disable forwarding: pci_bridge_disable_base_limit()
     * If disable forwarding is wanted, call pci_bridge_disable_base_limit()
     * after this function.
     */
    pci_byte_test_and_clear_mask(conf + PCI_IO_BASE,
                                 PCI_IO_RANGE_MASK & 0xff);
    pci_byte_test_and_clear_mask(conf + PCI_IO_LIMIT,
                                 PCI_IO_RANGE_MASK & 0xff);
    pci_word_test_and_clear_mask(conf + PCI_MEMORY_BASE,
                                 PCI_MEMORY_RANGE_MASK & 0xffff);
    pci_word_test_and_clear_mask(conf + PCI_MEMORY_LIMIT,
                                 PCI_MEMORY_RANGE_MASK & 0xffff);
    pci_word_test_and_clear_mask(conf + PCI_PREF_MEMORY_BASE,
                                 PCI_PREF_RANGE_MASK & 0xffff);
    pci_word_test_and_clear_mask(conf + PCI_PREF_MEMORY_LIMIT,
                                 PCI_PREF_RANGE_MASK & 0xffff);
    pci_set_long(conf + PCI_PREF_BASE_UPPER32, 0);
    pci_set_long(conf + PCI_PREF_LIMIT_UPPER32, 0);

    pci_set_word(conf + PCI_BRIDGE_CONTROL, 0);
}

/* default qdev initialization function for PCI-to-PCI bridge */
void pci_bridge_initfn(PCIDevice *dev, const char *typename)
{
    PCIBus *parent = pci_get_bus(dev);
    PCIBridge *br = PCI_BRIDGE(dev);
    PCIBus *sec_bus = &br->sec_bus;

    pci_word_test_and_set_mask(dev->config + PCI_STATUS,
                               PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);

    /*
     * TODO: We implement VGA Enable in the Bridge Control Register
     * therefore per the PCI to PCI bridge spec we must also implement
     * VGA Palette Snooping.  When done, set this bit writable:
     *
     * pci_word_test_and_set_mask(dev->wmask + PCI_COMMAND,
     *                            PCI_COMMAND_VGA_PALETTE);
     */

    pci_config_set_class(dev->config, PCI_CLASS_BRIDGE_PCI);
    dev->config[PCI_HEADER_TYPE] =
        (dev->config[PCI_HEADER_TYPE] & PCI_HEADER_TYPE_MULTI_FUNCTION) |
        PCI_HEADER_TYPE_BRIDGE;
    pci_set_word(dev->config + PCI_SEC_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);

    /*
     * If we don't specify the name, the bus will be addressed as <id>.0, where
     * id is the device id.
     * Since PCI Bridge devices have a single bus each, we don't need the index:
     * let users address the bus using the device name.
     */
    if (!br->bus_name && dev->qdev.id && *dev->qdev.id) {
            br->bus_name = dev->qdev.id;
    }

    qbus_init(sec_bus, sizeof(br->sec_bus), typename, DEVICE(dev),
              br->bus_name);
    sec_bus->parent_dev = dev;
    sec_bus->map_irq = br->map_irq ? br->map_irq : pci_swizzle_map_irq_fn;
    sec_bus->address_space_mem = &br->address_space_mem;
    memory_region_init(&br->address_space_mem, OBJECT(br), "pci_bridge_pci", UINT64_MAX);
    sec_bus->address_space_io = &br->address_space_io;
    memory_region_init(&br->address_space_io, OBJECT(br), "pci_bridge_io",
                       4 * GiB);
    br->windows = pci_bridge_region_init(br);
    QLIST_INIT(&sec_bus->child);
    QLIST_INSERT_HEAD(&parent->child, sec_bus, sibling);
}

/* default qdev clean up function for PCI-to-PCI bridge */
void pci_bridge_exitfn(PCIDevice *pci_dev)
{
    PCIBridge *s = PCI_BRIDGE(pci_dev);
    assert(QLIST_EMPTY(&s->sec_bus.child));
    QLIST_REMOVE(&s->sec_bus, sibling);
    pci_bridge_region_del(s, s->windows);
    pci_bridge_region_cleanup(s, s->windows);
    /* object_unparent() is called automatically during device deletion */
}

/*
 * before qdev initialization(qdev_init()), this function sets bus_name and
 * map_irq callback which are necessary for pci_bridge_initfn() to
 * initialize bus.
 */
void pci_bridge_map_irq(PCIBridge *br, const char* bus_name,
                        pci_map_irq_fn map_irq)
{
    br->map_irq = map_irq;
    br->bus_name = bus_name;
}


int pci_bridge_qemu_reserve_cap_init(PCIDevice *dev, int cap_offset,
                                     PCIResReserve res_reserve, Error **errp)
{
    if (res_reserve.mem_pref_32 != (uint64_t)-1 &&
        res_reserve.mem_pref_64 != (uint64_t)-1) {
        error_setg(errp,
                   "PCI resource reserve cap: PREF32 and PREF64 conflict");
        return -EINVAL;
    }

    if (res_reserve.mem_non_pref != (uint64_t)-1 &&
        res_reserve.mem_non_pref >= 4 * GiB) {
        error_setg(errp,
                   "PCI resource reserve cap: mem-reserve must be less than 4G");
        return -EINVAL;
    }

    if (res_reserve.mem_pref_32 != (uint64_t)-1 &&
        res_reserve.mem_pref_32 >= 4 * GiB) {
        error_setg(errp,
                   "PCI resource reserve cap: pref32-reserve  must be less than 4G");
        return -EINVAL;
    }

    if (res_reserve.bus == (uint32_t)-1 &&
        res_reserve.io == (uint64_t)-1 &&
        res_reserve.mem_non_pref == (uint64_t)-1 &&
        res_reserve.mem_pref_32 == (uint64_t)-1 &&
        res_reserve.mem_pref_64 == (uint64_t)-1) {
        return 0;
    }

    size_t cap_len = sizeof(PCIBridgeQemuCap);
    PCIBridgeQemuCap cap = {
            .len = cap_len,
            .type = REDHAT_PCI_CAP_RESOURCE_RESERVE,
            .bus_res = res_reserve.bus,
            .io = res_reserve.io,
            .mem = res_reserve.mem_non_pref,
            .mem_pref_32 = res_reserve.mem_pref_32,
            .mem_pref_64 = res_reserve.mem_pref_64
    };

    int offset = pci_add_capability(dev, PCI_CAP_ID_VNDR,
                                    cap_offset, cap_len, errp);
    if (offset < 0) {
        return offset;
    }

    memcpy(dev->config + offset + PCI_CAP_FLAGS,
           (char *)&cap + PCI_CAP_FLAGS,
           cap_len - PCI_CAP_FLAGS);
    return 0;
}

static const TypeInfo pci_bridge_type_info = {
    .name = TYPE_PCI_BRIDGE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBridge),
    .abstract = true,
};

static void pci_bridge_register_types(void)
{
    type_register_static(&pci_bridge_type_info);
}

type_init(pci_bridge_register_types)
