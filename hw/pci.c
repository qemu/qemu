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
#include "monitor.h"
#include "net.h"
#include "sysemu.h"

//#define DEBUG_PCI
#ifdef DEBUG_PCI
# define PCI_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define PCI_DPRINTF(format, ...)       do { } while (0)
#endif

struct PCIBus {
    BusState qbus;
    int bus_num;
    int devfn_min;
    pci_set_irq_fn set_irq;
    pci_map_irq_fn map_irq;
    pci_hotplug_fn hotplug;
    uint32_t config_reg; /* XXX: suppress */
    void *irq_opaque;
    PCIDevice *devices[256];
    PCIDevice *parent_dev;
    PCIBus *next;
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
static PCIBus *first_bus;

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

static inline int pci_bar(int reg)
{
    return reg == PCI_ROM_SLOT ? PCI_ROM_ADDRESS : PCI_BASE_ADDRESS_0 + reg * 4;
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
        pci_set_long(dev->config + pci_bar(r), dev->io_regions[r].type);
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

void pci_bus_new_inplace(PCIBus *bus, DeviceState *parent,
                         const char *name, int devfn_min)
{
    static int nbus = 0;

    qbus_create_inplace(&bus->qbus, &pci_bus_info, parent, name);
    bus->devfn_min = devfn_min;
    bus->next = first_bus;
    first_bus = bus;
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

static void pci_register_secondary_bus(PCIBus *bus,
                                       PCIDevice *dev,
                                       pci_map_irq_fn map_irq,
                                       const char *name)
{
    qbus_create_inplace(&bus->qbus, &pci_bus_info, &dev->qdev, name);
    bus->map_irq = map_irq;
    bus->parent_dev = dev;
    bus->next = dev->bus->next;
    dev->bus->next = bus;
}

int pci_bus_num(PCIBus *s)
{
    return s->bus_num;
}

static int get_pci_config_device(QEMUFile *f, void *pv, size_t size)
{
    PCIDevice *s = container_of(pv, PCIDevice, config);
    uint8_t config[PCI_CONFIG_SPACE_SIZE];
    int i;

    assert(size == sizeof config);
    qemu_get_buffer(f, config, sizeof config);
    for (i = 0; i < sizeof config; ++i)
        if ((config[i] ^ s->config[i]) & s->cmask[i] & ~s->wmask[i])
            return -EINVAL;
    memcpy(s->config, config, sizeof config);

    pci_update_mappings(s);

    return 0;
}

/* just put buffer */
static void put_pci_config_device(QEMUFile *f, void *pv, size_t size)
{
    const uint8_t *v = pv;
    qemu_put_buffer(f, v, size);
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
        VMSTATE_SINGLE(config, PCIDevice, 0, vmstate_info_pci_config,
                       typeof_field(PCIDevice,config)),
        VMSTATE_INT32_ARRAY_V(irq_state, PCIDevice, PCI_NUM_PINS, 2),
        VMSTATE_END_OF_LIST()
    }
};

void pci_device_save(PCIDevice *s, QEMUFile *f)
{
    vmstate_save_state(f, &vmstate_pci_device, s);
}

int pci_device_load(PCIDevice *s, QEMUFile *f)
{
    return vmstate_load_state(f, &vmstate_pci_device, s, s->version_id);
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
    if (dom != 0 || pci_find_bus(bus) == NULL)
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
        return pci_find_bus(0);
    }

    if (pci_parse_devaddr(devaddr, &dom, &bus, &slot) < 0) {
        return NULL;
    }

    *devfnp = slot << 3;
    return pci_find_bus(bus);
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
    dev->wmask[PCI_CACHE_LINE_SIZE] = 0xff;
    dev->wmask[PCI_INTERRUPT_LINE] = 0xff;
    pci_set_word(dev->wmask + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    for (i = PCI_CONFIG_HEADER_SIZE; i < PCI_CONFIG_SPACE_SIZE; ++i)
        dev->wmask[i] = 0xff;
}

/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev, PCIBus *bus,
                                         const char *name, int devfn,
                                         PCIConfigReadFunc *config_read,
                                         PCIConfigWriteFunc *config_write)
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
    pci_set_default_subsystem_id(pci_dev);
    pci_init_cmask(pci_dev);
    pci_init_wmask(pci_dev);

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
                                     config_read, config_write);
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
            isa_unassign_ioport(r->addr, r->size);
        } else {
            cpu_register_physical_memory(pci_to_cpu_addr(r->addr),
                                                     r->size,
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

    pci_unregister_io_regions(pci_dev);

    qemu_free_irqs(pci_dev->irq);
    pci_dev->bus->devices[pci_dev->devfn] = NULL;
    return 0;
}

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                            uint32_t size, int type,
                            PCIMapIORegionFunc *map_func)
{
    PCIIORegion *r;
    uint32_t addr;
    uint32_t wmask;

    if ((unsigned int)region_num >= PCI_NUM_REGIONS)
        return;

    if (size & (size-1)) {
        fprintf(stderr, "ERROR: PCI region size must be pow2 "
                    "type=0x%x, size=0x%x\n", type, size);
        exit(1);
    }

    r = &pci_dev->io_regions[region_num];
    r->addr = PCI_BAR_UNMAPPED;
    r->size = size;
    r->type = type;
    r->map_func = map_func;

    wmask = ~(size - 1);
    addr = pci_bar(region_num);
    if (region_num == PCI_ROM_SLOT) {
        /* ROM enable bit is writeable */
        wmask |= PCI_ROM_ADDRESS_ENABLE;
    }
    pci_set_long(pci_dev->config + addr, type);
    pci_set_long(pci_dev->wmask + addr, wmask);
    pci_set_long(pci_dev->cmask + addr, 0xffffffff);
}

static void pci_update_mappings(PCIDevice *d)
{
    PCIIORegion *r;
    int cmd, i;
    uint32_t last_addr, new_addr;

    cmd = pci_get_word(d->config + PCI_COMMAND);
    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (r->size != 0) {
            if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
                if (cmd & PCI_COMMAND_IO) {
                    new_addr = pci_get_long(d->config + pci_bar(i));
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
                    new_addr = pci_get_long(d->config + pci_bar(i));
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
                        last_addr == PCI_BAR_UNMAPPED) {
                        new_addr = PCI_BAR_UNMAPPED;
                    }
                } else {
                no_mem_map:
                    new_addr = PCI_BAR_UNMAPPED;
                }
            }
            /* now do the real mapping */
            if (new_addr != r->addr) {
                if (r->addr != PCI_BAR_UNMAPPED) {
                    if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
                        int class;
                        /* NOTE: specific hack for IDE in PC case:
                           only one byte must be mapped. */
                        class = pci_get_word(d->config + PCI_CLASS_DEVICE);
                        if (class == 0x0101 && r->size == 4) {
                            isa_unassign_ioport(r->addr + 2, 1);
                        } else {
                            isa_unassign_ioport(r->addr, r->size);
                        }
                    } else {
                        cpu_register_physical_memory(pci_to_cpu_addr(r->addr),
                                                     r->size,
                                                     IO_MEM_UNASSIGNED);
                        qemu_unregister_coalesced_mmio(r->addr, r->size);
                    }
                }
                r->addr = new_addr;
                if (r->addr != PCI_BAR_UNMAPPED) {
                    r->map_func(d, i, r->addr, r->size, r->type);
                }
            }
        }
    }
}

uint32_t pci_default_read_config(PCIDevice *d,
                                 uint32_t address, int len)
{
    uint32_t val;

    switch(len) {
    default:
    case 4:
	if (address <= 0xfc) {
            val = pci_get_long(d->config + address);
	    break;
	}
	/* fall through */
    case 2:
        if (address <= 0xfe) {
            val = pci_get_word(d->config + address);
	    break;
	}
	/* fall through */
    case 1:
        val = pci_get_byte(d->config + address);
        break;
    }
    return val;
}

void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val, int l)
{
    uint8_t orig[PCI_CONFIG_SPACE_SIZE];
    int i;

    /* not efficient, but simple */
    memcpy(orig, d->config, PCI_CONFIG_SPACE_SIZE);
    for(i = 0; i < l && addr < PCI_CONFIG_SPACE_SIZE; val >>= 8, ++i, ++addr) {
        uint8_t wmask = d->wmask[addr];
        d->config[addr] = (d->config[addr] & ~wmask) | (val & wmask);
    }
    if (memcmp(orig + PCI_BASE_ADDRESS_0, d->config + PCI_BASE_ADDRESS_0, 24)
        || ((orig[PCI_COMMAND] ^ d->config[PCI_COMMAND])
            & (PCI_COMMAND_MEMORY | PCI_COMMAND_IO)))
        pci_update_mappings(d);
}

void pci_data_write(void *opaque, uint32_t addr, uint32_t val, int len)
{
    PCIBus *s = opaque;
    PCIDevice *pci_dev;
    int config_addr, bus_num;

#if 0
    PCI_DPRINTF("pci_data_write: addr=%08x val=%08x len=%d\n",
                addr, val, len);
#endif
    bus_num = (addr >> 16) & 0xff;
    while (s && s->bus_num != bus_num)
        s = s->next;
    if (!s)
        return;
    pci_dev = s->devices[(addr >> 8) & 0xff];
    if (!pci_dev)
        return;
    config_addr = addr & 0xff;
    PCI_DPRINTF("pci_config_write: %s: addr=%02x val=%08x len=%d\n",
                pci_dev->name, config_addr, val, len);
    pci_dev->config_write(pci_dev, config_addr, val, len);
}

uint32_t pci_data_read(void *opaque, uint32_t addr, int len)
{
    PCIBus *s = opaque;
    PCIDevice *pci_dev;
    int config_addr, bus_num;
    uint32_t val;

    bus_num = (addr >> 16) & 0xff;
    while (s && s->bus_num != bus_num)
        s= s->next;
    if (!s)
        goto fail;
    pci_dev = s->devices[(addr >> 8) & 0xff];
    if (!pci_dev) {
    fail:
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
        goto the_end;
    }
    config_addr = addr & 0xff;
    val = pci_dev->config_read(pci_dev, config_addr, len);
    PCI_DPRINTF("pci_config_read: %s: addr=%02x val=%08x len=%d\n",
                pci_dev->name, config_addr, val, len);
 the_end:
#if 0
    PCI_DPRINTF("pci_data_read: addr=%08x val=%08x len=%d\n",
                addr, val, len);
#endif
    return val;
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

static void pci_info_device(PCIDevice *d)
{
    Monitor *mon = cur_mon;
    int i, class;
    PCIIORegion *r;
    const pci_class_desc *desc;

    monitor_printf(mon, "  Bus %2d, device %3d, function %d:\n",
                   d->bus->bus_num, PCI_SLOT(d->devfn), PCI_FUNC(d->devfn));
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
        monitor_printf(mon, "      BUS %d.\n", d->config[0x19]);
    }
    for(i = 0;i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (r->size != 0) {
            monitor_printf(mon, "      BAR%d: ", i);
            if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
                monitor_printf(mon, "I/O at 0x%04x [0x%04x].\n",
                               r->addr, r->addr + r->size - 1);
            } else {
                monitor_printf(mon, "32 bit memory at 0x%08x [0x%08x].\n",
                               r->addr, r->addr + r->size - 1);
            }
        }
    }
    monitor_printf(mon, "      id \"%s\"\n", d->qdev.id ? d->qdev.id : "");
    if (class == 0x0604 && d->config[0x19] != 0) {
        pci_for_each_device(d->config[0x19], pci_info_device);
    }
}

void pci_for_each_device(int bus_num, void (*fn)(PCIDevice *d))
{
    PCIBus *bus = first_bus;
    PCIDevice *d;
    int devfn;

    while (bus && bus->bus_num != bus_num)
        bus = bus->next;
    if (bus) {
        for(devfn = 0; devfn < 256; devfn++) {
            d = bus->devices[devfn];
            if (d)
                fn(d);
        }
    }
}

void pci_info(Monitor *mon)
{
    pci_for_each_device(0, pci_info_device);
}

static const char * const pci_nic_models[] = {
    "ne2k_pci",
    "i82551",
    "i82557b",
    "i82559er",
    "rtl8139",
    "e1000",
    "pcnet",
    "virtio",
    NULL
};

static const char * const pci_nic_names[] = {
    "ne2k_pci",
    "i82551",
    "i82557b",
    "i82559er",
    "rtl8139",
    "e1000",
    "pcnet",
    "virtio-net-pci",
    NULL
};

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

static void pci_bridge_write_config(PCIDevice *d,
                             uint32_t address, uint32_t val, int len)
{
    PCIBridge *s = (PCIBridge *)d;

    pci_default_write_config(d, address, val, len);
    s->bus.bus_num = d->config[PCI_SECONDARY_BUS];
}

PCIBus *pci_find_bus(int bus_num)
{
    PCIBus *bus = first_bus;

    while (bus && bus->bus_num != bus_num)
        bus = bus->next;

    return bus;
}

PCIDevice *pci_find_device(int bus_num, int slot, int function)
{
    PCIBus *bus = pci_find_bus(bus_num);

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
    pci_register_secondary_bus(&s->bus, &s->dev, map_irq, name);
    return &s->bus;
}

static int pci_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    PCIDeviceInfo *info = container_of(base, PCIDeviceInfo, qdev);
    PCIBus *bus;
    int devfn, rc;

    bus = FROM_QBUS(PCIBus, qdev_get_parent_bus(qdev));
    devfn = pci_dev->devfn;
    pci_dev = do_pci_register_device(pci_dev, bus, base->name, devfn,
                                     info->config_read, info->config_write);
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
    int offset = PCI_CONFIG_HEADER_SIZE;
    int i;
    for (i = PCI_CONFIG_HEADER_SIZE; i < PCI_CONFIG_SPACE_SIZE; ++i)
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
                   d->bus->bus_num, PCI_SLOT(d->devfn), PCI_FUNC(d->devfn),
                   pci_get_word(d->config + PCI_VENDOR_ID),
                   pci_get_word(d->config + PCI_DEVICE_ID),
                   pci_get_word(d->config + PCI_SUBSYSTEM_VENDOR_ID),
                   pci_get_word(d->config + PCI_SUBSYSTEM_ID));
    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];
        if (!r->size)
            continue;
        monitor_printf(mon, "%*sbar %d: %s at 0x%x [0x%x]\n", indent, "",
                       i, r->type & PCI_BASE_ADDRESS_SPACE_IO ? "i/o" : "mem",
                       r->addr, r->addr + r->size - 1);
    }
}

static PCIDeviceInfo bridge_info = {
    .qdev.name    = "pci-bridge",
    .qdev.size    = sizeof(PCIBridge),
    .init         = pci_bridge_initfn,
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
