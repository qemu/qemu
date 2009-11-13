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
#include "hw.h"
#include "pci.h"
#include "pci_host.h"
#include "monitor.h"
#include "net.h"
#include "sysemu.h"
#include "msix.h"

//#define DEBUG_PCI
#ifdef DEBUG_PCI
# define PCI_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define PCI_DPRINTF(format, ...)       do { } while (0)
#endif

struct PCIBus {
    BusState qbus;
    int devfn_min;
    pci_set_irq_fn set_irq;
    pci_map_irq_fn map_irq;
    pci_hotplug_fn hotplug;
    uint32_t config_reg; /* XXX: suppress */
    void *irq_opaque;
    PCIDevice *devices[256];
    PCIDevice *parent_dev;

    QLIST_HEAD(, PCIBus) child; /* this will be replaced by qdev later */
    QLIST_ENTRY(PCIBus) sibling;/* this will be replaced by qdev later */

    /* The bus IRQ state is the logical OR of the connected devices.
       Keep a count of the number of devices with raised IRQs.  */
    int nirq;
    int *irq_count;
};

static void pcibus_dev_print(Monitor *mon, DeviceState *dev, int indent);

static struct BusInfo pci_bus_info = {
    .name       = "PCI",
    .size       = sizeof(PCIBus),
    .print_dev  = pcibus_dev_print,
    .props      = (Property[]) {
        DEFINE_PROP_PCI_DEVFN("addr", PCIDevice, devfn, -1),
        DEFINE_PROP_END_OF_LIST()
    }
};

static void pci_update_mappings(PCIDevice *d);
static void pci_set_irq(void *opaque, int irq_num, int level);

target_phys_addr_t pci_mem_base;
static uint16_t pci_default_sub_vendor_id = PCI_SUBVENDOR_ID_REDHAT_QUMRANET;
static uint16_t pci_default_sub_device_id = PCI_SUBDEVICE_ID_QEMU;

struct PCIHostBus {
    int domain;
    struct PCIBus *bus;
    QLIST_ENTRY(PCIHostBus) next;
};
static QLIST_HEAD(, PCIHostBus) host_buses;

static const VMStateDescription vmstate_pcibus = {
    .name = "PCIBUS",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_INT32_EQUAL(nirq, PCIBus),
        VMSTATE_VARRAY_INT32(irq_count, PCIBus, nirq, 0, vmstate_info_int32, int32_t),
        VMSTATE_END_OF_LIST()
    }
};

static int pci_bar(PCIDevice *d, int reg)
{
    uint8_t type;

    if (reg != PCI_ROM_SLOT)
        return PCI_BASE_ADDRESS_0 + reg * 4;

    type = d->config[PCI_HEADER_TYPE] & ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    return type == PCI_HEADER_TYPE_BRIDGE ? PCI_ROM_ADDRESS1 : PCI_ROM_ADDRESS;
}

static void pci_device_reset(PCIDevice *dev)
{
    int r;

    memset(dev->irq_state, 0, sizeof dev->irq_state);
    dev->config[PCI_COMMAND] &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                                  PCI_COMMAND_MASTER);
    dev->config[PCI_CACHE_LINE_SIZE] = 0x0;
    dev->config[PCI_INTERRUPT_LINE] = 0x0;
    for (r = 0; r < PCI_NUM_REGIONS; ++r) {
        if (!dev->io_regions[r].size) {
            continue;
        }
        pci_set_long(dev->config + pci_bar(dev, r), dev->io_regions[r].type);
    }
    pci_update_mappings(dev);
}

static void pci_bus_reset(void *opaque)
{
    PCIBus *bus = opaque;
    int i;

    for (i = 0; i < bus->nirq; i++) {
        bus->irq_count[i] = 0;
    }
    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        if (bus->devices[i]) {
            pci_device_reset(bus->devices[i]);
        }
    }
}

static void pci_host_bus_register(int domain, PCIBus *bus)
{
    struct PCIHostBus *host;
    host = qemu_mallocz(sizeof(*host));
    host->domain = domain;
    host->bus = bus;
    QLIST_INSERT_HEAD(&host_buses, host, next);
}

PCIBus *pci_find_host_bus(int domain)
{
    struct PCIHostBus *host;

    QLIST_FOREACH(host, &host_buses, next) {
        if (host->domain == domain) {
            return host->bus;
        }
    }

    return NULL;
}

void pci_bus_new_inplace(PCIBus *bus, DeviceState *parent,
                         const char *name, int devfn_min)
{
    static int nbus = 0;

    qbus_create_inplace(&bus->qbus, &pci_bus_info, parent, name);
    bus->devfn_min = devfn_min;

    /* host bridge */
    QLIST_INIT(&bus->child);
    pci_host_bus_register(0, bus); /* for now only pci domain 0 is supported */

    vmstate_register(nbus++, &vmstate_pcibus, bus);
    qemu_register_reset(pci_bus_reset, bus);
}

PCIBus *pci_bus_new(DeviceState *parent, const char *name, int devfn_min)
{
    PCIBus *bus;

    bus = qemu_mallocz(sizeof(*bus));
    bus->qbus.qdev_allocated = 1;
    pci_bus_new_inplace(bus, parent, name, devfn_min);
    return bus;
}

void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                  void *irq_opaque, int nirq)
{
    bus->set_irq = set_irq;
    bus->map_irq = map_irq;
    bus->irq_opaque = irq_opaque;
    bus->nirq = nirq;
    bus->irq_count = qemu_mallocz(nirq * sizeof(bus->irq_count[0]));
}

void pci_bus_hotplug(PCIBus *bus, pci_hotplug_fn hotplug)
{
    bus->qbus.allow_hotplug = 1;
    bus->hotplug = hotplug;
}

PCIBus *pci_register_bus(DeviceState *parent, const char *name,
                         pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                         void *irq_opaque, int devfn_min, int nirq)
{
    PCIBus *bus;

    bus = pci_bus_new(parent, name, devfn_min);
    pci_bus_irqs(bus, set_irq, map_irq, irq_opaque, nirq);
    return bus;
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

int pci_bus_num(PCIBus *s)
{
    if (!s->parent_dev)
        return 0;       /* pci host bridge */
    return s->parent_dev->config[PCI_SECONDARY_BUS];
}

static uint8_t pci_sub_bus(PCIBus *s)
{
    if (!s->parent_dev)
        return 255;     /* pci host bridge */
    return s->parent_dev->config[PCI_SUBORDINATE_BUS];
}

static int get_pci_config_device(QEMUFile *f, void *pv, size_t size)
{
    PCIDevice *s = container_of(pv, PCIDevice, config);
    uint8_t *config;
    int i;

    assert(size == pci_config_size(s));
    config = qemu_malloc(size);

    qemu_get_buffer(f, config, size);
    for (i = 0; i < size; ++i) {
        if ((config[i] ^ s->config[i]) & s->cmask[i] & ~s->wmask[i]) {
            qemu_free(config);
            return -EINVAL;
        }
    }
    memcpy(s->config, config, size);

    pci_update_mappings(s);

    qemu_free(config);
    return 0;
}

/* just put buffer */
static void put_pci_config_device(QEMUFile *f, void *pv, size_t size)
{
    const uint8_t **v = pv;
    assert(size == pci_config_size(container_of(pv, PCIDevice, config)));
    qemu_put_buffer(f, *v, size);
}

static VMStateInfo vmstate_info_pci_config = {
    .name = "pci config",
    .get  = get_pci_config_device,
    .put  = put_pci_config_device,
};

const VMStateDescription vmstate_pci_device = {
    .name = "PCIDevice",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_INT32_LE(version_id, PCIDevice),
        VMSTATE_BUFFER_UNSAFE_INFO(config, PCIDevice, 0,
                                   vmstate_info_pci_config,
                                   PCI_CONFIG_SPACE_SIZE),
        VMSTATE_INT32_ARRAY_V(irq_state, PCIDevice, PCI_NUM_PINS, 2),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_pcie_device = {
    .name = "PCIDevice",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_INT32_LE(version_id, PCIDevice),
        VMSTATE_BUFFER_UNSAFE_INFO(config, PCIDevice, 0,
                                   vmstate_info_pci_config,
                                   PCIE_CONFIG_SPACE_SIZE),
        VMSTATE_INT32_ARRAY_V(irq_state, PCIDevice, PCI_NUM_PINS, 2),
        VMSTATE_END_OF_LIST()
    }
};

static inline const VMStateDescription *pci_get_vmstate(PCIDevice *s)
{
    return pci_is_express(s) ? &vmstate_pcie_device : &vmstate_pci_device;
}

void pci_device_save(PCIDevice *s, QEMUFile *f)
{
    vmstate_save_state(f, pci_get_vmstate(s), s);
}

int pci_device_load(PCIDevice *s, QEMUFile *f)
{
    return vmstate_load_state(f, pci_get_vmstate(s), s, s->version_id);
}

static int pci_set_default_subsystem_id(PCIDevice *pci_dev)
{
    uint16_t *id;

    id = (void*)(&pci_dev->config[PCI_SUBVENDOR_ID]);
    id[0] = cpu_to_le16(pci_default_sub_vendor_id);
    id[1] = cpu_to_le16(pci_default_sub_device_id);
    return 0;
}

/*
 * Parse [[<domain>:]<bus>:]<slot>, return -1 on error
 */
static int pci_parse_devaddr(const char *addr, int *domp, int *busp, unsigned *slotp)
{
    const char *p;
    char *e;
    unsigned long val;
    unsigned long dom = 0, bus = 0;
    unsigned slot = 0;

    p = addr;
    val = strtoul(p, &e, 16);
    if (e == p)
	return -1;
    if (*e == ':') {
	bus = val;
	p = e + 1;
	val = strtoul(p, &e, 16);
	if (e == p)
	    return -1;
	if (*e == ':') {
	    dom = bus;
	    bus = val;
	    p = e + 1;
	    val = strtoul(p, &e, 16);
	    if (e == p)
		return -1;
	}
    }

    if (dom > 0xffff || bus > 0xff || val > 0x1f)
	return -1;

    slot = val;

    if (*e)
	return -1;

    /* Note: QEMU doesn't implement domains other than 0 */
    if (!pci_find_bus(pci_find_host_bus(dom), bus))
	return -1;

    *domp = dom;
    *busp = bus;
    *slotp = slot;
    return 0;
}

int pci_read_devaddr(Monitor *mon, const char *addr, int *domp, int *busp,
                     unsigned *slotp)
{
    /* strip legacy tag */
    if (!strncmp(addr, "pci_addr=", 9)) {
        addr += 9;
    }
    if (pci_parse_devaddr(addr, domp, busp, slotp)) {
        monitor_printf(mon, "Invalid pci address\n");
        return -1;
    }
    return 0;
}

PCIBus *pci_get_bus_devfn(int *devfnp, const char *devaddr)
{
    int dom, bus;
    unsigned slot;

    if (!devaddr) {
        *devfnp = -1;
        return pci_find_bus(pci_find_host_bus(0), 0);
    }

    if (pci_parse_devaddr(devaddr, &dom, &bus, &slot) < 0) {
        return NULL;
    }

    *devfnp = slot << 3;
    return pci_find_bus(pci_find_host_bus(0), bus);
}

static void pci_init_cmask(PCIDevice *dev)
{
    pci_set_word(dev->cmask + PCI_VENDOR_ID, 0xffff);
    pci_set_word(dev->cmask + PCI_DEVICE_ID, 0xffff);
    dev->cmask[PCI_STATUS] = PCI_STATUS_CAP_LIST;
    dev->cmask[PCI_REVISION_ID] = 0xff;
    dev->cmask[PCI_CLASS_PROG] = 0xff;
    pci_set_word(dev->cmask + PCI_CLASS_DEVICE, 0xffff);
    dev->cmask[PCI_HEADER_TYPE] = 0xff;
    dev->cmask[PCI_CAPABILITY_LIST] = 0xff;
}

static void pci_init_wmask(PCIDevice *dev)
{
    int i;
    int config_size = pci_config_size(dev);

    dev->wmask[PCI_CACHE_LINE_SIZE] = 0xff;
    dev->wmask[PCI_INTERRUPT_LINE] = 0xff;
    pci_set_word(dev->wmask + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    for (i = PCI_CONFIG_HEADER_SIZE; i < config_size; ++i)
        dev->wmask[i] = 0xff;
}

static void pci_init_wmask_bridge(PCIDevice *d)
{
    /* PCI_PRIMARY_BUS, PCI_SECONDARY_BUS, PCI_SUBORDINATE_BUS and
       PCI_SEC_LETENCY_TIMER */
    memset(d->wmask + PCI_PRIMARY_BUS, 0xff, 4);

    /* base and limit */
    d->wmask[PCI_IO_BASE] = PCI_IO_RANGE_MASK & 0xff;
    d->wmask[PCI_IO_LIMIT] = PCI_IO_RANGE_MASK & 0xff;
    pci_set_word(d->wmask + PCI_MEMORY_BASE,
                 PCI_MEMORY_RANGE_MASK & 0xffff);
    pci_set_word(d->wmask + PCI_MEMORY_LIMIT,
                 PCI_MEMORY_RANGE_MASK & 0xffff);
    pci_set_word(d->wmask + PCI_PREF_MEMORY_BASE,
                 PCI_PREF_RANGE_MASK & 0xffff);
    pci_set_word(d->wmask + PCI_PREF_MEMORY_LIMIT,
                 PCI_PREF_RANGE_MASK & 0xffff);

    /* PCI_PREF_BASE_UPPER32 and PCI_PREF_LIMIT_UPPER32 */
    memset(d->wmask + PCI_PREF_BASE_UPPER32, 0xff, 8);

    pci_set_word(d->wmask + PCI_BRIDGE_CONTROL, 0xffff);
}

static void pci_config_alloc(PCIDevice *pci_dev)
{
    int config_size = pci_config_size(pci_dev);

    pci_dev->config = qemu_mallocz(config_size);
    pci_dev->cmask = qemu_mallocz(config_size);
    pci_dev->wmask = qemu_mallocz(config_size);
    pci_dev->used = qemu_mallocz(config_size);
}

static void pci_config_free(PCIDevice *pci_dev)
{
    qemu_free(pci_dev->config);
    qemu_free(pci_dev->cmask);
    qemu_free(pci_dev->wmask);
    qemu_free(pci_dev->used);
}

/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev, PCIBus *bus,
                                         const char *name, int devfn,
                                         PCIConfigReadFunc *config_read,
                                         PCIConfigWriteFunc *config_write,
                                         uint8_t header_type)
{
    if (devfn < 0) {
        for(devfn = bus->devfn_min ; devfn < 256; devfn += 8) {
            if (!bus->devices[devfn])
                goto found;
        }
        return NULL;
    found: ;
    } else if (bus->devices[devfn]) {
        return NULL;
    }
    pci_dev->bus = bus;
    pci_dev->devfn = devfn;
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);
    memset(pci_dev->irq_state, 0, sizeof(pci_dev->irq_state));
    pci_config_alloc(pci_dev);

    header_type &= ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    if (header_type == PCI_HEADER_TYPE_NORMAL) {
        pci_set_default_subsystem_id(pci_dev);
    }
    pci_init_cmask(pci_dev);
    pci_init_wmask(pci_dev);
    if (header_type == PCI_HEADER_TYPE_BRIDGE) {
        pci_init_wmask_bridge(pci_dev);
    }

    if (!config_read)
        config_read = pci_default_read_config;
    if (!config_write)
        config_write = pci_default_write_config;
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    bus->devices[devfn] = pci_dev;
    pci_dev->irq = qemu_allocate_irqs(pci_set_irq, pci_dev, PCI_NUM_PINS);
    pci_dev->version_id = 2; /* Current pci device vmstate version */
    return pci_dev;
}

PCIDevice *pci_register_device(PCIBus *bus, const char *name,
                               int instance_size, int devfn,
                               PCIConfigReadFunc *config_read,
                               PCIConfigWriteFunc *config_write)
{
    PCIDevice *pci_dev;

    pci_dev = qemu_mallocz(instance_size);
    pci_dev = do_pci_register_device(pci_dev, bus, name, devfn,
                                     config_read, config_write,
                                     PCI_HEADER_TYPE_NORMAL);
    return pci_dev;
}
static target_phys_addr_t pci_to_cpu_addr(target_phys_addr_t addr)
{
    return addr + pci_mem_base;
}

static void pci_unregister_io_regions(PCIDevice *pci_dev)
{
    PCIIORegion *r;
    int i;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &pci_dev->io_regions[i];
        if (!r->size || r->addr == PCI_BAR_UNMAPPED)
            continue;
        if (r->type == PCI_BASE_ADDRESS_SPACE_IO) {
            isa_unassign_ioport(r->addr, r->filtered_size);
        } else {
            cpu_register_physical_memory(pci_to_cpu_addr(r->addr),
                                                     r->filtered_size,
                                                     IO_MEM_UNASSIGNED);
        }
    }
}

static int pci_unregister_device(DeviceState *dev)
{
    PCIDevice *pci_dev = DO_UPCAST(PCIDevice, qdev, dev);
    PCIDeviceInfo *info = DO_UPCAST(PCIDeviceInfo, qdev, dev->info);
    int ret = 0;

    if (info->exit)
        ret = info->exit(pci_dev);
    if (ret)
        return ret;

    msix_uninit(pci_dev);
    pci_unregister_io_regions(pci_dev);

    qemu_free_irqs(pci_dev->irq);
    pci_dev->bus->devices[pci_dev->devfn] = NULL;
    pci_config_free(pci_dev);
    return 0;
}

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                            pcibus_t size, int type,
                            PCIMapIORegionFunc *map_func)
{
    PCIIORegion *r;
    uint32_t addr;
    pcibus_t wmask;

    if ((unsigned int)region_num >= PCI_NUM_REGIONS)
        return;

    if (size & (size-1)) {
        fprintf(stderr, "ERROR: PCI region size must be pow2 "
                    "type=0x%x, size=0x%"FMT_PCIBUS"\n", type, size);
        exit(1);
    }

    r = &pci_dev->io_regions[region_num];
    r->addr = PCI_BAR_UNMAPPED;
    r->size = size;
    r->filtered_size = size;
    r->type = type;
    r->map_func = map_func;

    wmask = ~(size - 1);
    addr = pci_bar(pci_dev, region_num);
    if (region_num == PCI_ROM_SLOT) {
        /* ROM enable bit is writeable */
        wmask |= PCI_ROM_ADDRESS_ENABLE;
    }
    pci_set_long(pci_dev->config + addr, type);
    if (!(r->type & PCI_BASE_ADDRESS_SPACE_IO) &&
        r->type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
        pci_set_quad(pci_dev->wmask + addr, wmask);
        pci_set_quad(pci_dev->cmask + addr, ~0ULL);
    } else {
        pci_set_long(pci_dev->wmask + addr, wmask & 0xffffffff);
        pci_set_long(pci_dev->cmask + addr, 0xffffffff);
    }
}

static uint32_t pci_config_get_io_base(PCIDevice *d,
                                       uint32_t base, uint32_t base_upper16)
{
    uint32_t val;

    val = ((uint32_t)d->config[base] & PCI_IO_RANGE_MASK) << 8;
    if (d->config[base] & PCI_IO_RANGE_TYPE_32) {
        val |= (uint32_t)pci_get_word(d->config + PCI_IO_BASE_UPPER16) << 16;
    }
    return val;
}

static uint64_t pci_config_get_memory_base(PCIDevice *d, uint32_t base)
{
    return ((uint64_t)pci_get_word(d->config + base) & PCI_MEMORY_RANGE_MASK)
        << 16;
}

static uint64_t pci_config_get_pref_base(PCIDevice *d,
                                         uint32_t base, uint32_t upper)
{
    uint64_t val;
    val = ((uint64_t)pci_get_word(d->config + base) &
           PCI_PREF_RANGE_MASK) << 16;
    val |= (uint64_t)pci_get_long(d->config + upper) << 32;
    return val;
}

static pcibus_t pci_bridge_get_base(PCIDevice *bridge, uint8_t type)
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

static pcibus_t pci_bridge_get_limit(PCIDevice *bridge, uint8_t type)
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

static void pci_bridge_filter(PCIDevice *d, pcibus_t *addr, pcibus_t *size,
                              uint8_t type)
{
    pcibus_t base = *addr;
    pcibus_t limit = *addr + *size - 1;
    PCIDevice *br;

    for (br = d->bus->parent_dev; br; br = br->bus->parent_dev) {
        uint16_t cmd = pci_get_word(d->config + PCI_COMMAND);

        if (type & PCI_BASE_ADDRESS_SPACE_IO) {
            if (!(cmd & PCI_COMMAND_IO)) {
                goto no_map;
            }
        } else {
            if (!(cmd & PCI_COMMAND_MEMORY)) {
                goto no_map;
            }
        }

        base = MAX(base, pci_bridge_get_base(br, type));
        limit = MIN(limit, pci_bridge_get_limit(br, type));
    }

    if (base > limit) {
    no_map:
        *addr = PCI_BAR_UNMAPPED;
        *size = 0;
    } else {
        *addr = base;
        *size = limit - base + 1;
    }
}

static void pci_update_mappings(PCIDevice *d)
{
    PCIIORegion *r;
    int cmd, i;
    pcibus_t last_addr, new_addr;
    pcibus_t filtered_size;

    cmd = pci_get_word(d->config + PCI_COMMAND);
    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];

        /* this region isn't registered */
        if (r->size == 0)
            continue;

        if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
            if (cmd & PCI_COMMAND_IO) {
                new_addr = pci_get_long(d->config + pci_bar(d, i));
                new_addr = new_addr & ~(r->size - 1);
                last_addr = new_addr + r->size - 1;
                /* NOTE: we have only 64K ioports on PC */
                if (last_addr <= new_addr || new_addr == 0 ||
                    last_addr >= 0x10000) {
                    new_addr = PCI_BAR_UNMAPPED;
                }
            } else {
                new_addr = PCI_BAR_UNMAPPED;
            }
        } else {
            if (cmd & PCI_COMMAND_MEMORY) {
                if (r->type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
                    new_addr = pci_get_quad(d->config + pci_bar(d, i));
                } else {
                    new_addr = pci_get_long(d->config + pci_bar(d, i));
                }
                /* the ROM slot has a specific enable bit */
                if (i == PCI_ROM_SLOT && !(new_addr & PCI_ROM_ADDRESS_ENABLE))
                    goto no_mem_map;
                new_addr = new_addr & ~(r->size - 1);
                last_addr = new_addr + r->size - 1;
                /* NOTE: we do not support wrapping */
                /* XXX: as we cannot support really dynamic
                   mappings, we handle specific values as invalid
                   mappings. */
                if (last_addr <= new_addr || new_addr == 0 ||
                    last_addr == PCI_BAR_UNMAPPED ||

                    /* Now pcibus_t is 64bit.
                     * Check if 32 bit BAR wrap around explicitly.
                     * Without this, PC ide doesn't work well.
                     * TODO: remove this work around.
                     */
                    (!(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64) &&
                     last_addr >= UINT32_MAX) ||

                    /*
                     * OS is allowed to set BAR beyond its addressable
                     * bits. For example, 32 bit OS can set 64bit bar
                     * to >4G. Check it.
                     */
                    last_addr >= TARGET_PHYS_ADDR_MAX) {
                    new_addr = PCI_BAR_UNMAPPED;
                }
            } else {
            no_mem_map:
                new_addr = PCI_BAR_UNMAPPED;
            }
        }

        /* bridge filtering */
        filtered_size = r->size;
        if (new_addr != PCI_BAR_UNMAPPED) {
            pci_bridge_filter(d, &new_addr, &filtered_size, r->type);
        }

        /* This bar isn't changed */
        if (new_addr == r->addr && filtered_size == r->filtered_size)
            continue;

        /* now do the real mapping */
        if (r->addr != PCI_BAR_UNMAPPED) {
            if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
                int class;
                /* NOTE: specific hack for IDE in PC case:
                   only one byte must be mapped. */
                class = pci_get_word(d->config + PCI_CLASS_DEVICE);
                if (class == 0x0101 && r->size == 4) {
                    isa_unassign_ioport(r->addr + 2, 1);
                } else {
                    isa_unassign_ioport(r->addr, r->filtered_size);
                }
            } else {
                cpu_register_physical_memory(pci_to_cpu_addr(r->addr),
                                             r->filtered_size,
                                             IO_MEM_UNASSIGNED);
                qemu_unregister_coalesced_mmio(r->addr, r->filtered_size);
            }
        }
        r->addr = new_addr;
        r->filtered_size = filtered_size;
        if (r->addr != PCI_BAR_UNMAPPED) {
            /*
             * TODO: currently almost all the map funcions assumes
             * filtered_size == size and addr & ~(size - 1) == addr.
             * However with bridge filtering, they aren't always true.
             * Teach them such cases, such that filtered_size < size and
             * addr & (size - 1) != 0.
             */
            r->map_func(d, i, r->addr, r->filtered_size, r->type);
        }
    }
}

uint32_t pci_default_read_config(PCIDevice *d,
                                 uint32_t address, int len)
{
    uint32_t val = 0;
    assert(len == 1 || len == 2 || len == 4);
    len = MIN(len, pci_config_size(d) - address);
    memcpy(&val, d->config + address, len);
    return le32_to_cpu(val);
}

void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val, int l)
{
    int i;
    uint32_t config_size = pci_config_size(d);

    for (i = 0; i < l && addr + i < config_size; val >>= 8, ++i) {
        uint8_t wmask = d->wmask[addr + i];
        d->config[addr + i] = (d->config[addr + i] & ~wmask) | (val & wmask);
    }
    if (ranges_overlap(addr, l, PCI_BASE_ADDRESS_0, 24) ||
        ranges_overlap(addr, l, PCI_ROM_ADDRESS, 4) ||
        ranges_overlap(addr, l, PCI_ROM_ADDRESS1, 4) ||
        range_covers_byte(addr, l, PCI_COMMAND))
        pci_update_mappings(d);
}

/***********************************************************/
/* generic PCI irq support */

/* 0 <= irq_num <= 3. level must be 0 or 1 */
static void pci_set_irq(void *opaque, int irq_num, int level)
{
    PCIDevice *pci_dev = opaque;
    PCIBus *bus;
    int change;

    change = level - pci_dev->irq_state[irq_num];
    if (!change)
        return;

    pci_dev->irq_state[irq_num] = level;
    for (;;) {
        bus = pci_dev->bus;
        irq_num = bus->map_irq(pci_dev, irq_num);
        if (bus->set_irq)
            break;
        pci_dev = bus->parent_dev;
    }
    bus->irq_count[irq_num] += change;
    bus->set_irq(bus->irq_opaque, irq_num, bus->irq_count[irq_num] != 0);
}

/***********************************************************/
/* monitor info on PCI */

typedef struct {
    uint16_t class;
    const char *desc;
} pci_class_desc;

static const pci_class_desc pci_class_descriptions[] =
{
    { 0x0100, "SCSI controller"},
    { 0x0101, "IDE controller"},
    { 0x0102, "Floppy controller"},
    { 0x0103, "IPI controller"},
    { 0x0104, "RAID controller"},
    { 0x0106, "SATA controller"},
    { 0x0107, "SAS controller"},
    { 0x0180, "Storage controller"},
    { 0x0200, "Ethernet controller"},
    { 0x0201, "Token Ring controller"},
    { 0x0202, "FDDI controller"},
    { 0x0203, "ATM controller"},
    { 0x0280, "Network controller"},
    { 0x0300, "VGA controller"},
    { 0x0301, "XGA controller"},
    { 0x0302, "3D controller"},
    { 0x0380, "Display controller"},
    { 0x0400, "Video controller"},
    { 0x0401, "Audio controller"},
    { 0x0402, "Phone"},
    { 0x0480, "Multimedia controller"},
    { 0x0500, "RAM controller"},
    { 0x0501, "Flash controller"},
    { 0x0580, "Memory controller"},
    { 0x0600, "Host bridge"},
    { 0x0601, "ISA bridge"},
    { 0x0602, "EISA bridge"},
    { 0x0603, "MC bridge"},
    { 0x0604, "PCI bridge"},
    { 0x0605, "PCMCIA bridge"},
    { 0x0606, "NUBUS bridge"},
    { 0x0607, "CARDBUS bridge"},
    { 0x0608, "RACEWAY bridge"},
    { 0x0680, "Bridge"},
    { 0x0c03, "USB controller"},
    { 0, NULL}
};

static void pci_info_device(PCIBus *bus, PCIDevice *d)
{
    Monitor *mon = cur_mon;
    int i, class;
    PCIIORegion *r;
    const pci_class_desc *desc;

    monitor_printf(mon, "  Bus %2d, device %3d, function %d:\n",
                   pci_bus_num(d->bus),
                   PCI_SLOT(d->devfn), PCI_FUNC(d->devfn));
    class = pci_get_word(d->config + PCI_CLASS_DEVICE);
    monitor_printf(mon, "    ");
    desc = pci_class_descriptions;
    while (desc->desc && class != desc->class)
        desc++;
    if (desc->desc) {
        monitor_printf(mon, "%s", desc->desc);
    } else {
        monitor_printf(mon, "Class %04x", class);
    }
    monitor_printf(mon, ": PCI device %04x:%04x\n",
           pci_get_word(d->config + PCI_VENDOR_ID),
           pci_get_word(d->config + PCI_DEVICE_ID));

    if (d->config[PCI_INTERRUPT_PIN] != 0) {
        monitor_printf(mon, "      IRQ %d.\n",
                       d->config[PCI_INTERRUPT_LINE]);
    }
    if (class == 0x0604) {
        uint64_t base;
        uint64_t limit;

        monitor_printf(mon, "      BUS %d.\n", d->config[0x19]);
        monitor_printf(mon, "      secondary bus %d.\n",
                       d->config[PCI_SECONDARY_BUS]);
        monitor_printf(mon, "      subordinate bus %d.\n",
                       d->config[PCI_SUBORDINATE_BUS]);

        base = pci_bridge_get_base(d, PCI_BASE_ADDRESS_SPACE_IO);
        limit = pci_bridge_get_limit(d, PCI_BASE_ADDRESS_SPACE_IO);
        monitor_printf(mon, "      IO range [0x%04"PRIx64", 0x%04"PRIx64"]\n",
                       base, limit);

        base = pci_bridge_get_base(d, PCI_BASE_ADDRESS_SPACE_MEMORY);
        limit= pci_config_get_memory_base(d, PCI_BASE_ADDRESS_SPACE_MEMORY);
        monitor_printf(mon,
                       "      memory range [0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       base, limit);

        base = pci_bridge_get_base(d, PCI_BASE_ADDRESS_SPACE_MEMORY |
                                   PCI_BASE_ADDRESS_MEM_PREFETCH);
        limit = pci_bridge_get_limit(d, PCI_BASE_ADDRESS_SPACE_MEMORY |
                                     PCI_BASE_ADDRESS_MEM_PREFETCH);
        monitor_printf(mon, "      prefetchable memory range "
                       "[0x%08"PRIx64", 0x%08"PRIx64"]\n", base, limit);
    }
    for(i = 0;i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (r->size != 0) {
            monitor_printf(mon, "      BAR%d: ", i);
            if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
                monitor_printf(mon, "I/O at 0x%04"FMT_PCIBUS
                               " [0x%04"FMT_PCIBUS"].\n",
                               r->addr, r->addr + r->size - 1);
            } else {
                const char *type = r->type & PCI_BASE_ADDRESS_MEM_TYPE_64 ?
                    "64 bit" : "32 bit";
                const char *prefetch =
                    r->type & PCI_BASE_ADDRESS_MEM_PREFETCH ?
                    " prefetchable" : "";

                monitor_printf(mon, "%s%s memory at 0x%08"FMT_PCIBUS
                               " [0x%08"FMT_PCIBUS"].\n",
                               type, prefetch,
                               r->addr, r->addr + r->size - 1);
            }
        }
    }
    monitor_printf(mon, "      id \"%s\"\n", d->qdev.id ? d->qdev.id : "");
    if (class == 0x0604 && d->config[0x19] != 0) {
        pci_for_each_device(bus, d->config[0x19], pci_info_device);
    }
}

static void pci_for_each_device_under_bus(PCIBus *bus,
                                          void (*fn)(PCIBus *b, PCIDevice *d))
{
    PCIDevice *d;
    int devfn;

    for(devfn = 0; devfn < 256; devfn++) {
        d = bus->devices[devfn];
        if (d)
            fn(bus, d);
    }
}

void pci_for_each_device(PCIBus *bus, int bus_num,
                         void (*fn)(PCIBus *b, PCIDevice *d))
{
    bus = pci_find_bus(bus, bus_num);

    if (bus) {
        pci_for_each_device_under_bus(bus, fn);
    }
}

void pci_info(Monitor *mon)
{
    struct PCIHostBus *host;
    QLIST_FOREACH(host, &host_buses, next) {
        pci_for_each_device(host->bus, 0, pci_info_device);
    }
}

static const char * const pci_nic_models[] = {
#if !defined(CONFIG_WIN32)
    "atheros_wlan",
#endif
    "dp83816",
    "e100",
    "ne2k_pci",
    "i82551",
    "i82557a",
    "i82557b",
    "i82557c",
    "i82558b",
    "i82559c",
    "i82559er",
    "rtl8139",
    "e1000",
    "pcnet",
    "tnetw1130",
    "virtio",
    NULL
};

static const char * const pci_nic_names[] = {
#if !defined(CONFIG_WIN32)
    "atheros_wlan",
#endif
    "dp83816",
    "e100",
    "ne2k_pci",
    "i82551",
    "i82557a",
    "i82557b",
    "i82557c",
    "i82558b",
    "i82559c",
    "i82559er",
    "rtl8139",
    "e1000",
    "pcnet",
    "tnetw1130",
    "virtio-net-pci",
    NULL
};

int pci_nic_supported(const char *model)
{
    int i;

    for (i = 0; pci_nic_names[i]; i++)
        if (strcmp(model, pci_nic_names[i]) == 0)
            return 1;

    return 0;
}

/* Initialize a PCI NIC.  */
/* FIXME callers should check for failure, but don't */
PCIDevice *pci_nic_init(NICInfo *nd, const char *default_model,
                        const char *default_devaddr)
{
    const char *devaddr = nd->devaddr ? nd->devaddr : default_devaddr;
    PCIBus *bus;
    int devfn;
    PCIDevice *pci_dev;
    DeviceState *dev;
    int i;

    i = qemu_find_nic_model(nd, pci_nic_models, default_model);
    if (i < 0)
        return NULL;

    bus = pci_get_bus_devfn(&devfn, devaddr);
    if (!bus) {
        qemu_error("Invalid PCI device address %s for device %s\n",
                   devaddr, pci_nic_names[i]);
        return NULL;
    }

    pci_dev = pci_create(bus, devfn, pci_nic_names[i]);
    dev = &pci_dev->qdev;
    if (nd->name)
        dev->id = qemu_strdup(nd->name);
    qdev_set_nic_properties(dev, nd);
    if (qdev_init(dev) < 0)
        return NULL;
    return pci_dev;
}

PCIDevice *pci_nic_init_nofail(NICInfo *nd, const char *default_model,
                               const char *default_devaddr)
{
    PCIDevice *res;

    if (qemu_show_nic_models(nd->model, pci_nic_models))
        exit(0);

    res = pci_nic_init(nd, default_model, default_devaddr);
    if (!res)
        exit(1);
    return res;
}

typedef struct {
    PCIDevice dev;
    PCIBus bus;
    uint32_t vid;
    uint32_t did;
} PCIBridge;


static void pci_bridge_update_mappings_fn(PCIBus *b, PCIDevice *d)
{
    pci_update_mappings(d);
}

static void pci_bridge_update_mappings(PCIBus *b)
{
    PCIBus *child;

    pci_for_each_device_under_bus(b, pci_bridge_update_mappings_fn);

    QLIST_FOREACH(child, &b->child, sibling) {
        pci_bridge_update_mappings(child);
    }
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
        pci_bridge_update_mappings(d->bus);
    }
}

PCIBus *pci_find_bus(PCIBus *bus, int bus_num)
{
    PCIBus *sec;

    if (!bus)
        return NULL;

    if (pci_bus_num(bus) == bus_num) {
        return bus;
    }

    /* try child bus */
    QLIST_FOREACH(sec, &bus->child, sibling) {
        if (pci_bus_num(sec) <= bus_num && bus_num <= pci_sub_bus(sec)) {
            return pci_find_bus(sec, bus_num);
        }
    }

    return NULL;
}

PCIDevice *pci_find_device(PCIBus *bus, int bus_num, int slot, int function)
{
    bus = pci_find_bus(bus, bus_num);

    if (!bus)
        return NULL;

    return bus->devices[PCI_DEVFN(slot, function)];
}

static int pci_bridge_initfn(PCIDevice *dev)
{
    PCIBridge *s = DO_UPCAST(PCIBridge, dev, dev);

    pci_config_set_vendor_id(s->dev.config, s->vid);
    pci_config_set_device_id(s->dev.config, s->did);

    /* TODO: intial value
     * command register:
     * According to PCI bridge spec, after reset
     *   bus master bit is off
     *   memory space enable bit is off
     * According to manual (805-1251.pdf).(See abp_pbi.c for its links.)
     *   the reset value should be zero unless the boot pin is tied high
     *   (which is tru) and thus it should be PCI_COMMAND_MEMORY.
     *
     * For now, don't touch the value.
     * Later command register will be set to zero and apb_pci.c will
     * override the value.
     * Same for latency timer, and multi function bit of header type.
     */
    pci_set_word(dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    pci_set_word(dev->config + PCI_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    pci_config_set_class(dev->config, PCI_CLASS_BRIDGE_PCI);
    dev->config[PCI_LATENCY_TIMER] = 0x10;
    dev->config[PCI_HEADER_TYPE] =
        PCI_HEADER_TYPE_MULTI_FUNCTION | PCI_HEADER_TYPE_BRIDGE;
    pci_set_word(dev->config + PCI_SEC_STATUS,
                 PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK);
    return 0;
}

static int pci_bridge_exitfn(PCIDevice *pci_dev)
{
    PCIBridge *s = DO_UPCAST(PCIBridge, dev, pci_dev);
    PCIBus *bus = &s->bus;
    pci_unregister_secondary_bus(bus);
    return 0;
}

PCIBus *pci_bridge_init(PCIBus *bus, int devfn, uint16_t vid, uint16_t did,
                        pci_map_irq_fn map_irq, const char *name)
{
    PCIDevice *dev;
    PCIBridge *s;

    dev = pci_create(bus, devfn, "pci-bridge");
    qdev_prop_set_uint32(&dev->qdev, "vendorid", vid);
    qdev_prop_set_uint32(&dev->qdev, "deviceid", did);
    qdev_init_nofail(&dev->qdev);

    s = DO_UPCAST(PCIBridge, dev, dev);
    pci_register_secondary_bus(bus, &s->bus, &s->dev, map_irq, name);
    return &s->bus;
}

static int pci_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    PCIDeviceInfo *info = container_of(base, PCIDeviceInfo, qdev);
    PCIBus *bus;
    int devfn, rc;

    /* initialize cap_present for pci_is_express() and pci_config_size() */
    if (info->is_express) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    bus = FROM_QBUS(PCIBus, qdev_get_parent_bus(qdev));
    devfn = pci_dev->devfn;
    pci_dev = do_pci_register_device(pci_dev, bus, base->name, devfn,
                                     info->config_read, info->config_write,
                                     info->header_type);
    assert(pci_dev);
    rc = info->init(pci_dev);
    if (rc != 0)
        return rc;
    if (qdev->hotplugged)
        bus->hotplug(pci_dev, 1);
    return 0;
}

static int pci_unplug_device(DeviceState *qdev)
{
    PCIDevice *dev = DO_UPCAST(PCIDevice, qdev, qdev);

    dev->bus->hotplug(dev, 0);
    return 0;
}

void pci_qdev_register(PCIDeviceInfo *info)
{
    info->qdev.init = pci_qdev_init;
    info->qdev.unplug = pci_unplug_device;
    info->qdev.exit = pci_unregister_device;
    info->qdev.bus_info = &pci_bus_info;
    qdev_register(&info->qdev);
}

void pci_qdev_register_many(PCIDeviceInfo *info)
{
    while (info->qdev.name) {
        pci_qdev_register(info);
        info++;
    }
}

PCIDevice *pci_create(PCIBus *bus, int devfn, const char *name)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, name);
    qdev_prop_set_uint32(dev, "addr", devfn);
    return DO_UPCAST(PCIDevice, qdev, dev);
}

PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name)
{
    PCIDevice *dev = pci_create(bus, devfn, name);
    qdev_init_nofail(&dev->qdev);
    return dev;
}

static int pci_find_space(PCIDevice *pdev, uint8_t size)
{
    int config_size = pci_config_size(pdev);
    int offset = PCI_CONFIG_HEADER_SIZE;
    int i;
    for (i = PCI_CONFIG_HEADER_SIZE; i < config_size; ++i)
        if (pdev->used[i])
            offset = i + 1;
        else if (i - offset + 1 == size)
            return offset;
    return 0;
}

static uint8_t pci_find_capability_list(PCIDevice *pdev, uint8_t cap_id,
                                        uint8_t *prev_p)
{
    uint8_t next, prev;

    if (!(pdev->config[PCI_STATUS] & PCI_STATUS_CAP_LIST))
        return 0;

    for (prev = PCI_CAPABILITY_LIST; (next = pdev->config[prev]);
         prev = next + PCI_CAP_LIST_NEXT)
        if (pdev->config[next + PCI_CAP_LIST_ID] == cap_id)
            break;

    if (prev_p)
        *prev_p = prev;
    return next;
}

/* Reserve space and add capability to the linked list in pci config space */
int pci_add_capability(PCIDevice *pdev, uint8_t cap_id, uint8_t size)
{
    uint8_t offset = pci_find_space(pdev, size);
    uint8_t *config = pdev->config + offset;
    if (!offset)
        return -ENOSPC;
    config[PCI_CAP_LIST_ID] = cap_id;
    config[PCI_CAP_LIST_NEXT] = pdev->config[PCI_CAPABILITY_LIST];
    pdev->config[PCI_CAPABILITY_LIST] = offset;
    pdev->config[PCI_STATUS] |= PCI_STATUS_CAP_LIST;
    memset(pdev->used + offset, 0xFF, size);
    /* Make capability read-only by default */
    memset(pdev->wmask + offset, 0, size);
    /* Check capability by default */
    memset(pdev->cmask + offset, 0xFF, size);
    return offset;
}

/* Unlink capability from the pci config space. */
void pci_del_capability(PCIDevice *pdev, uint8_t cap_id, uint8_t size)
{
    uint8_t prev, offset = pci_find_capability_list(pdev, cap_id, &prev);
    if (!offset)
        return;
    pdev->config[prev] = pdev->config[offset + PCI_CAP_LIST_NEXT];
    /* Make capability writeable again */
    memset(pdev->wmask + offset, 0xff, size);
    /* Clear cmask as device-specific registers can't be checked */
    memset(pdev->cmask + offset, 0, size);
    memset(pdev->used + offset, 0, size);

    if (!pdev->config[PCI_CAPABILITY_LIST])
        pdev->config[PCI_STATUS] &= ~PCI_STATUS_CAP_LIST;
}

/* Reserve space for capability at a known offset (to call after load). */
void pci_reserve_capability(PCIDevice *pdev, uint8_t offset, uint8_t size)
{
    memset(pdev->used + offset, 0xff, size);
}

uint8_t pci_find_capability(PCIDevice *pdev, uint8_t cap_id)
{
    return pci_find_capability_list(pdev, cap_id, NULL);
}

static void pcibus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    PCIDevice *d = (PCIDevice *)dev;
    const pci_class_desc *desc;
    char ctxt[64];
    PCIIORegion *r;
    int i, class;

    class = pci_get_word(d->config + PCI_CLASS_DEVICE);
    desc = pci_class_descriptions;
    while (desc->desc && class != desc->class)
        desc++;
    if (desc->desc) {
        snprintf(ctxt, sizeof(ctxt), "%s", desc->desc);
    } else {
        snprintf(ctxt, sizeof(ctxt), "Class %04x", class);
    }

    monitor_printf(mon, "%*sclass %s, addr %02x:%02x.%x, "
                   "pci id %04x:%04x (sub %04x:%04x)\n",
                   indent, "", ctxt,
                   d->config[PCI_SECONDARY_BUS],
                   PCI_SLOT(d->devfn), PCI_FUNC(d->devfn),
                   pci_get_word(d->config + PCI_VENDOR_ID),
                   pci_get_word(d->config + PCI_DEVICE_ID),
                   pci_get_word(d->config + PCI_SUBSYSTEM_VENDOR_ID),
                   pci_get_word(d->config + PCI_SUBSYSTEM_ID));
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (!r->size)
            continue;
        monitor_printf(mon, "%*sbar %d: %s at 0x%"FMT_PCIBUS
                       " [0x%"FMT_PCIBUS"]\n",
                       indent, "",
                       i, r->type & PCI_BASE_ADDRESS_SPACE_IO ? "i/o" : "mem",
                       r->addr, r->addr + r->size - 1);
    }
}

static PCIDeviceInfo bridge_info = {
    .qdev.name    = "pci-bridge",
    .qdev.size    = sizeof(PCIBridge),
    .init         = pci_bridge_initfn,
    .exit         = pci_bridge_exitfn,
    .config_write = pci_bridge_write_config,
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
