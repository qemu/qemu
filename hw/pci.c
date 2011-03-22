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
#include "pci_bridge.h"
#include "pci_internals.h"
#include "monitor.h"
#include "net.h"
#include "sysemu.h"
#include "loader.h"
#include "qemu-objects.h"
#include "range.h"

//#define DEBUG_PCI
#ifdef DEBUG_PCI
# define PCI_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define PCI_DPRINTF(format, ...)       do { } while (0)
#endif

static void pcibus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *pcibus_get_dev_path(DeviceState *dev);
static char *pcibus_get_fw_dev_path(DeviceState *dev);
static int pcibus_reset(BusState *qbus);

struct BusInfo pci_bus_info = {
    .name       = "PCI",
    .size       = sizeof(PCIBus),
    .print_dev  = pcibus_dev_print,
    .get_dev_path = pcibus_get_dev_path,
    .get_fw_dev_path = pcibus_get_fw_dev_path,
    .reset      = pcibus_reset,
    .props      = (Property[]) {
        DEFINE_PROP_PCI_DEVFN("addr", PCIDevice, devfn, -1),
        DEFINE_PROP_STRING("romfile", PCIDevice, romfile),
        DEFINE_PROP_UINT32("rombar",  PCIDevice, rom_bar, 1),
        DEFINE_PROP_BIT("multifunction", PCIDevice, cap_present,
                        QEMU_PCI_CAP_MULTIFUNCTION_BITNR, false),
        DEFINE_PROP_BIT("command_serr_enable", PCIDevice, cap_present,
                        QEMU_PCI_CAP_SERR_BITNR, true),
        DEFINE_PROP_END_OF_LIST()
    }
};

static void pci_update_mappings(PCIDevice *d);
static void pci_set_irq(void *opaque, int irq_num, int level);
static int pci_add_option_rom(PCIDevice *pdev, bool is_default_rom);
static void pci_del_option_rom(PCIDevice *pdev);

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

static inline int pci_irq_state(PCIDevice *d, int irq_num)
{
	return (d->irq_state >> irq_num) & 0x1;
}

static inline void pci_set_irq_state(PCIDevice *d, int irq_num, int level)
{
	d->irq_state &= ~(0x1 << irq_num);
	d->irq_state |= level << irq_num;
}

static void pci_change_irq_level(PCIDevice *pci_dev, int irq_num, int change)
{
    PCIBus *bus;
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

int pci_bus_get_irq_level(PCIBus *bus, int irq_num)
{
    assert(irq_num >= 0);
    assert(irq_num < bus->nirq);
    return !!bus->irq_count[irq_num];
}

/* Update interrupt status bit in config space on interrupt
 * state change. */
static void pci_update_irq_status(PCIDevice *dev)
{
    if (dev->irq_state) {
        dev->config[PCI_STATUS] |= PCI_STATUS_INTERRUPT;
    } else {
        dev->config[PCI_STATUS] &= ~PCI_STATUS_INTERRUPT;
    }
}

void pci_device_deassert_intx(PCIDevice *dev)
{
    int i;
    for (i = 0; i < PCI_NUM_PINS; ++i) {
        qemu_set_irq(dev->irq[i], 0);
    }
}

/*
 * This function is called on #RST and FLR.
 * FLR if PCI_EXP_DEVCTL_BCR_FLR is set
 */
void pci_device_reset(PCIDevice *dev)
{
    int r;
    /* TODO: call the below unconditionally once all pci devices
     * are qdevified */
    if (dev->qdev.info) {
        qdev_reset_all(&dev->qdev);
    }

    dev->irq_state = 0;
    pci_update_irq_status(dev);
    pci_device_deassert_intx(dev);
    /* Clear all writeable bits */
    pci_word_test_and_clear_mask(dev->config + PCI_COMMAND,
                                 pci_get_word(dev->wmask + PCI_COMMAND) |
                                 pci_get_word(dev->w1cmask + PCI_COMMAND));
    pci_word_test_and_clear_mask(dev->config + PCI_STATUS,
                                 pci_get_word(dev->wmask + PCI_STATUS) |
                                 pci_get_word(dev->w1cmask + PCI_STATUS));
    dev->config[PCI_CACHE_LINE_SIZE] = 0x0;
    dev->config[PCI_INTERRUPT_LINE] = 0x0;
    for (r = 0; r < PCI_NUM_REGIONS; ++r) {
        PCIIORegion *region = &dev->io_regions[r];
        if (!region->size) {
            continue;
        }

        if (!(region->type & PCI_BASE_ADDRESS_SPACE_IO) &&
            region->type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
            pci_set_quad(dev->config + pci_bar(dev, r), region->type);
        } else {
            pci_set_long(dev->config + pci_bar(dev, r), region->type);
        }
    }
    pci_update_mappings(dev);
}

/*
 * Trigger pci bus reset under a given bus.
 * To be called on RST# assert.
 */
void pci_bus_reset(PCIBus *bus)
{
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

static int pcibus_reset(BusState *qbus)
{
    pci_bus_reset(DO_UPCAST(PCIBus, qbus, qbus));

    /* topology traverse is done by pci_bus_reset().
       Tell qbus/qdev walker not to traverse the tree */
    return 1;
}

static void pci_host_bus_register(int domain, PCIBus *bus)
{
    struct PCIHostBus *host;
    host = qemu_mallocz(sizeof(*host));
    host->domain = domain;
    host->bus = bus;
    QLIST_INSERT_HEAD(&host_buses, host, next);
}

PCIBus *pci_find_root_bus(int domain)
{
    struct PCIHostBus *host;

    QLIST_FOREACH(host, &host_buses, next) {
        if (host->domain == domain) {
            return host->bus;
        }
    }

    return NULL;
}

int pci_find_domain(const PCIBus *bus)
{
    PCIDevice *d;
    struct PCIHostBus *host;

    /* obtain root bus */
    while ((d = bus->parent_dev) != NULL) {
        bus = d->bus;
    }

    QLIST_FOREACH(host, &host_buses, next) {
        if (host->bus == bus) {
            return host->domain;
        }
    }

    abort();    /* should not be reached */
    return -1;
}

void pci_bus_new_inplace(PCIBus *bus, DeviceState *parent,
                         const char *name, uint8_t devfn_min)
{
    qbus_create_inplace(&bus->qbus, &pci_bus_info, parent, name);
    assert(PCI_FUNC(devfn_min) == 0);
    bus->devfn_min = devfn_min;

    /* host bridge */
    QLIST_INIT(&bus->child);
    pci_host_bus_register(0, bus); /* for now only pci domain 0 is supported */

    vmstate_register(NULL, -1, &vmstate_pcibus, bus);
}

PCIBus *pci_bus_new(DeviceState *parent, const char *name, uint8_t devfn_min)
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

void pci_bus_hotplug(PCIBus *bus, pci_hotplug_fn hotplug, DeviceState *qdev)
{
    bus->qbus.allow_hotplug = 1;
    bus->hotplug = hotplug;
    bus->hotplug_qdev = qdev;
}

void pci_bus_set_mem_base(PCIBus *bus, target_phys_addr_t base)
{
    bus->mem_base = base;
}

PCIBus *pci_register_bus(DeviceState *parent, const char *name,
                         pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                         void *irq_opaque, uint8_t devfn_min, int nirq)
{
    PCIBus *bus;

    bus = pci_bus_new(parent, name, devfn_min);
    pci_bus_irqs(bus, set_irq, map_irq, irq_opaque, nirq);
    return bus;
}

int pci_bus_num(PCIBus *s)
{
    if (!s->parent_dev)
        return 0;       /* pci host bridge */
    return s->parent_dev->config[PCI_SECONDARY_BUS];
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
        if ((config[i] ^ s->config[i]) &
            s->cmask[i] & ~s->wmask[i] & ~s->w1cmask[i]) {
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

static int get_pci_irq_state(QEMUFile *f, void *pv, size_t size)
{
    PCIDevice *s = container_of(pv, PCIDevice, irq_state);
    uint32_t irq_state[PCI_NUM_PINS];
    int i;
    for (i = 0; i < PCI_NUM_PINS; ++i) {
        irq_state[i] = qemu_get_be32(f);
        if (irq_state[i] != 0x1 && irq_state[i] != 0) {
            fprintf(stderr, "irq state %d: must be 0 or 1.\n",
                    irq_state[i]);
            return -EINVAL;
        }
    }

    for (i = 0; i < PCI_NUM_PINS; ++i) {
        pci_set_irq_state(s, i, irq_state[i]);
    }

    return 0;
}

static void put_pci_irq_state(QEMUFile *f, void *pv, size_t size)
{
    int i;
    PCIDevice *s = container_of(pv, PCIDevice, irq_state);

    for (i = 0; i < PCI_NUM_PINS; ++i) {
        qemu_put_be32(f, pci_irq_state(s, i));
    }
}

static VMStateInfo vmstate_info_pci_irq_state = {
    .name = "pci irq state",
    .get  = get_pci_irq_state,
    .put  = put_pci_irq_state,
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
        VMSTATE_BUFFER_UNSAFE_INFO(irq_state, PCIDevice, 2,
				   vmstate_info_pci_irq_state,
				   PCI_NUM_PINS * sizeof(int32_t)),
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
        VMSTATE_BUFFER_UNSAFE_INFO(irq_state, PCIDevice, 2,
				   vmstate_info_pci_irq_state,
				   PCI_NUM_PINS * sizeof(int32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static inline const VMStateDescription *pci_get_vmstate(PCIDevice *s)
{
    return pci_is_express(s) ? &vmstate_pcie_device : &vmstate_pci_device;
}

void pci_device_save(PCIDevice *s, QEMUFile *f)
{
    /* Clear interrupt status bit: it is implicit
     * in irq_state which we are saving.
     * This makes us compatible with old devices
     * which never set or clear this bit. */
    s->config[PCI_STATUS] &= ~PCI_STATUS_INTERRUPT;
    vmstate_save_state(f, pci_get_vmstate(s), s);
    /* Restore the interrupt status bit. */
    pci_update_irq_status(s);
}

int pci_device_load(PCIDevice *s, QEMUFile *f)
{
    int ret;
    ret = vmstate_load_state(f, pci_get_vmstate(s), s, s->version_id);
    /* Restore the interrupt status bit. */
    pci_update_irq_status(s);
    return ret;
}

static void pci_set_default_subsystem_id(PCIDevice *pci_dev)
{
    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID,
                 pci_default_sub_vendor_id);
    pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID,
                 pci_default_sub_device_id);
}

/*
 * Parse [[<domain>:]<bus>:]<slot>, return -1 on error if funcp == NULL
 *       [[<domain>:]<bus>:]<slot>.<func>, return -1 on error
 */
int pci_parse_devaddr(const char *addr, int *domp, int *busp,
                      unsigned int *slotp, unsigned int *funcp)
{
    const char *p;
    char *e;
    unsigned long val;
    unsigned long dom = 0, bus = 0;
    unsigned int slot = 0;
    unsigned int func = 0;

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

    slot = val;

    if (funcp != NULL) {
        if (*e != '.')
            return -1;

        p = e + 1;
        val = strtoul(p, &e, 16);
        if (e == p)
            return -1;

        func = val;
    }

    /* if funcp == NULL func is 0 */
    if (dom > 0xffff || bus > 0xff || slot > 0x1f || func > 7)
	return -1;

    if (*e)
	return -1;

    /* Note: QEMU doesn't implement domains other than 0 */
    if (!pci_find_bus(pci_find_root_bus(dom), bus))
	return -1;

    *domp = dom;
    *busp = bus;
    *slotp = slot;
    if (funcp != NULL)
        *funcp = func;
    return 0;
}

int pci_read_devaddr(Monitor *mon, const char *addr, int *domp, int *busp,
                     unsigned *slotp)
{
    /* strip legacy tag */
    if (!strncmp(addr, "pci_addr=", 9)) {
        addr += 9;
    }
    if (pci_parse_devaddr(addr, domp, busp, slotp, NULL)) {
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
        return pci_find_bus(pci_find_root_bus(0), 0);
    }

    if (pci_parse_devaddr(devaddr, &dom, &bus, &slot, NULL) < 0) {
        return NULL;
    }

    *devfnp = PCI_DEVFN(slot, 0);
    return pci_find_bus(pci_find_root_bus(dom), bus);
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
    int config_size = pci_config_size(dev);

    dev->wmask[PCI_CACHE_LINE_SIZE] = 0xff;
    dev->wmask[PCI_INTERRUPT_LINE] = 0xff;
    pci_set_word(dev->wmask + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
                 PCI_COMMAND_INTX_DISABLE);
    if (dev->cap_present & QEMU_PCI_CAP_SERR) {
        pci_word_test_and_set_mask(dev->wmask + PCI_COMMAND, PCI_COMMAND_SERR);
    }

    memset(dev->wmask + PCI_CONFIG_HEADER_SIZE, 0xff,
           config_size - PCI_CONFIG_HEADER_SIZE);
}

static void pci_init_w1cmask(PCIDevice *dev)
{
    /*
     * Note: It's okay to set w1cmask even for readonly bits as
     * long as their value is hardwired to 0.
     */
    pci_set_word(dev->w1cmask + PCI_STATUS,
                 PCI_STATUS_PARITY | PCI_STATUS_SIG_TARGET_ABORT |
                 PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_REC_MASTER_ABORT |
                 PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY);
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

/* TODO: add this define to pci_regs.h in linux and then in qemu. */
#define  PCI_BRIDGE_CTL_VGA_16BIT	0x10	/* VGA 16-bit decode */
#define  PCI_BRIDGE_CTL_DISCARD		0x100	/* Primary discard timer */
#define  PCI_BRIDGE_CTL_SEC_DISCARD	0x200	/* Secondary discard timer */
#define  PCI_BRIDGE_CTL_DISCARD_STATUS	0x400	/* Discard timer status */
#define  PCI_BRIDGE_CTL_DISCARD_SERR	0x800	/* Discard timer SERR# enable */
    pci_set_word(d->wmask + PCI_BRIDGE_CONTROL,
                 PCI_BRIDGE_CTL_PARITY |
                 PCI_BRIDGE_CTL_SERR |
                 PCI_BRIDGE_CTL_ISA |
                 PCI_BRIDGE_CTL_VGA |
                 PCI_BRIDGE_CTL_VGA_16BIT |
                 PCI_BRIDGE_CTL_MASTER_ABORT |
                 PCI_BRIDGE_CTL_BUS_RESET |
                 PCI_BRIDGE_CTL_FAST_BACK |
                 PCI_BRIDGE_CTL_DISCARD |
                 PCI_BRIDGE_CTL_SEC_DISCARD |
                 PCI_BRIDGE_CTL_DISCARD_SERR);
    /* Below does not do anything as we never set this bit, put here for
     * completeness. */
    pci_set_word(d->w1cmask + PCI_BRIDGE_CONTROL,
                 PCI_BRIDGE_CTL_DISCARD_STATUS);
}

static int pci_init_multifunction(PCIBus *bus, PCIDevice *dev)
{
    uint8_t slot = PCI_SLOT(dev->devfn);
    uint8_t func;

    if (dev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        dev->config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
    }

    /*
     * multifunction bit is interpreted in two ways as follows.
     *   - all functions must set the bit to 1.
     *     Example: Intel X53
     *   - function 0 must set the bit, but the rest function (> 0)
     *     is allowed to leave the bit to 0.
     *     Example: PIIX3(also in qemu), PIIX4(also in qemu), ICH10,
     *
     * So OS (at least Linux) checks the bit of only function 0,
     * and doesn't see the bit of function > 0.
     *
     * The below check allows both interpretation.
     */
    if (PCI_FUNC(dev->devfn)) {
        PCIDevice *f0 = bus->devices[PCI_DEVFN(slot, 0)];
        if (f0 && !(f0->cap_present & QEMU_PCI_CAP_MULTIFUNCTION)) {
            /* function 0 should set multifunction bit */
            error_report("PCI: single function device can't be populated "
                         "in function %x.%x", slot, PCI_FUNC(dev->devfn));
            return -1;
        }
        return 0;
    }

    if (dev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        return 0;
    }
    /* function 0 indicates single function, so function > 0 must be NULL */
    for (func = 1; func < PCI_FUNC_MAX; ++func) {
        if (bus->devices[PCI_DEVFN(slot, func)]) {
            error_report("PCI: %x.0 indicates single function, "
                         "but %x.%x is already populated.",
                         slot, slot, func);
            return -1;
        }
    }
    return 0;
}

static void pci_config_alloc(PCIDevice *pci_dev)
{
    int config_size = pci_config_size(pci_dev);

    pci_dev->config = qemu_mallocz(config_size);
    pci_dev->cmask = qemu_mallocz(config_size);
    pci_dev->wmask = qemu_mallocz(config_size);
    pci_dev->w1cmask = qemu_mallocz(config_size);
    pci_dev->used = qemu_mallocz(config_size);
}

static void pci_config_free(PCIDevice *pci_dev)
{
    qemu_free(pci_dev->config);
    qemu_free(pci_dev->cmask);
    qemu_free(pci_dev->wmask);
    qemu_free(pci_dev->w1cmask);
    qemu_free(pci_dev->used);
}

/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev, PCIBus *bus,
                                         const char *name, int devfn,
                                         PCIConfigReadFunc *config_read,
                                         PCIConfigWriteFunc *config_write,
                                         bool is_bridge)
{
    if (devfn < 0) {
        for(devfn = bus->devfn_min ; devfn < ARRAY_SIZE(bus->devices);
            devfn += PCI_FUNC_MAX) {
            if (!bus->devices[devfn])
                goto found;
        }
        error_report("PCI: no slot/function available for %s, all in use", name);
        return NULL;
    found: ;
    } else if (bus->devices[devfn]) {
        error_report("PCI: slot %d function %d not available for %s, in use by %s",
                     PCI_SLOT(devfn), PCI_FUNC(devfn), name, bus->devices[devfn]->name);
        return NULL;
    }
    pci_dev->bus = bus;
    pci_dev->devfn = devfn;
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);
    pci_dev->irq_state = 0;
    pci_config_alloc(pci_dev);

    if (!is_bridge) {
        pci_set_default_subsystem_id(pci_dev);
    }
    pci_init_cmask(pci_dev);
    pci_init_wmask(pci_dev);
    pci_init_w1cmask(pci_dev);
    if (is_bridge) {
        pci_init_wmask_bridge(pci_dev);
    }
    if (pci_init_multifunction(bus, pci_dev)) {
        pci_config_free(pci_dev);
        return NULL;
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

static void do_pci_unregister_device(PCIDevice *pci_dev)
{
    qemu_free_irqs(pci_dev->irq);
    pci_dev->bus->devices[pci_dev->devfn] = NULL;
    pci_config_free(pci_dev);
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
    if (pci_dev == NULL) {
        hw_error("PCI: can't register device\n");
    }
    return pci_dev;
}

static target_phys_addr_t pci_to_cpu_addr(PCIBus *bus,
                                          target_phys_addr_t addr)
{
    return addr + bus->mem_base;
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
            cpu_register_physical_memory(pci_to_cpu_addr(pci_dev->bus,
                                                         r->addr),
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

    pci_unregister_io_regions(pci_dev);
    pci_del_option_rom(pci_dev);
    qemu_free(pci_dev->romfile);
    do_pci_unregister_device(pci_dev);
    return 0;
}

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                            pcibus_t size, uint8_t type,
                            PCIMapIORegionFunc *map_func)
{
    PCIIORegion *r;
    uint32_t addr;
    uint64_t wmask;

    assert(region_num >= 0);
    assert(region_num < PCI_NUM_REGIONS);
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
    r->ram_addr = IO_MEM_UNASSIGNED;

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

static void pci_simple_bar_mapfunc(PCIDevice *pci_dev, int region_num,
                                   pcibus_t addr, pcibus_t size, int type)
{
    cpu_register_physical_memory(addr, size,
                                 pci_dev->io_regions[region_num].ram_addr);
}

void pci_register_bar_simple(PCIDevice *pci_dev, int region_num,
                             pcibus_t size,  uint8_t attr, ram_addr_t ram_addr)
{
    pci_register_bar(pci_dev, region_num, size,
                     PCI_BASE_ADDRESS_SPACE_MEMORY | attr,
                     pci_simple_bar_mapfunc);
    pci_dev->io_regions[region_num].ram_addr = ram_addr;
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
        goto no_map;
    }
    *addr = base;
    *size = limit - base + 1;
    return;
no_map:
    *addr = PCI_BAR_UNMAPPED;
    *size = 0;
}

static pcibus_t pci_bar_address(PCIDevice *d,
				int reg, uint8_t type, pcibus_t size)
{
    pcibus_t new_addr, last_addr;
    int bar = pci_bar(d, reg);
    uint16_t cmd = pci_get_word(d->config + PCI_COMMAND);

    if (type & PCI_BASE_ADDRESS_SPACE_IO) {
        if (!(cmd & PCI_COMMAND_IO)) {
            return PCI_BAR_UNMAPPED;
        }
        new_addr = pci_get_long(d->config + bar) & ~(size - 1);
        last_addr = new_addr + size - 1;
        /* NOTE: we have only 64K ioports on PC */
        if (last_addr <= new_addr || new_addr == 0 || last_addr > UINT16_MAX) {
            return PCI_BAR_UNMAPPED;
        }
        return new_addr;
    }

    if (!(cmd & PCI_COMMAND_MEMORY)) {
        return PCI_BAR_UNMAPPED;
    }
    if (type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
        new_addr = pci_get_quad(d->config + bar);
    } else {
        new_addr = pci_get_long(d->config + bar);
    }
    /* the ROM slot has a specific enable bit */
    if (reg == PCI_ROM_SLOT && !(new_addr & PCI_ROM_ADDRESS_ENABLE)) {
        return PCI_BAR_UNMAPPED;
    }
    new_addr &= ~(size - 1);
    last_addr = new_addr + size - 1;
    /* NOTE: we do not support wrapping */
    /* XXX: as we cannot support really dynamic
       mappings, we handle specific values as invalid
       mappings. */
    if (last_addr <= new_addr || new_addr == 0 ||
        last_addr == PCI_BAR_UNMAPPED) {
        return PCI_BAR_UNMAPPED;
    }

    /* Now pcibus_t is 64bit.
     * Check if 32 bit BAR wraps around explicitly.
     * Without this, PC ide doesn't work well.
     * TODO: remove this work around.
     */
    if  (!(type & PCI_BASE_ADDRESS_MEM_TYPE_64) && last_addr >= UINT32_MAX) {
        return PCI_BAR_UNMAPPED;
    }

    /*
     * OS is allowed to set BAR beyond its addressable
     * bits. For example, 32 bit OS can set 64bit bar
     * to >4G. Check it. TODO: we might need to support
     * it in the future for e.g. PAE.
     */
    if (last_addr >= TARGET_PHYS_ADDR_MAX) {
        return PCI_BAR_UNMAPPED;
    }

    return new_addr;
}

static void pci_update_mappings(PCIDevice *d)
{
    PCIIORegion *r;
    int i;
    pcibus_t new_addr, filtered_size;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];

        /* this region isn't registered */
        if (!r->size)
            continue;

        new_addr = pci_bar_address(d, i, r->type, r->size);

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
                cpu_register_physical_memory(pci_to_cpu_addr(d->bus, r->addr),
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
            if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
                r->map_func(d, i, r->addr, r->filtered_size, r->type);
            } else {
                r->map_func(d, i, pci_to_cpu_addr(d->bus, r->addr),
                            r->filtered_size, r->type);
            }
        }
    }
}

static inline int pci_irq_disabled(PCIDevice *d)
{
    return pci_get_word(d->config + PCI_COMMAND) & PCI_COMMAND_INTX_DISABLE;
}

/* Called after interrupt disabled field update in config space,
 * assert/deassert interrupts if necessary.
 * Gets original interrupt disable bit value (before update). */
static void pci_update_irq_disabled(PCIDevice *d, int was_irq_disabled)
{
    int i, disabled = pci_irq_disabled(d);
    if (disabled == was_irq_disabled)
        return;
    for (i = 0; i < PCI_NUM_PINS; ++i) {
        int state = pci_irq_state(d, i);
        pci_change_irq_level(d, i, disabled ? -state : state);
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
    int i, was_irq_disabled = pci_irq_disabled(d);
    uint32_t config_size = pci_config_size(d);

    for (i = 0; i < l && addr + i < config_size; val >>= 8, ++i) {
        uint8_t wmask = d->wmask[addr + i];
        uint8_t w1cmask = d->w1cmask[addr + i];
        assert(!(wmask & w1cmask));
        d->config[addr + i] = (d->config[addr + i] & ~wmask) | (val & wmask);
        d->config[addr + i] &= ~(val & w1cmask); /* W1C: Write 1 to Clear */
    }
    if (ranges_overlap(addr, l, PCI_BASE_ADDRESS_0, 24) ||
        ranges_overlap(addr, l, PCI_ROM_ADDRESS, 4) ||
        ranges_overlap(addr, l, PCI_ROM_ADDRESS1, 4) ||
        range_covers_byte(addr, l, PCI_COMMAND))
        pci_update_mappings(d);

    if (range_covers_byte(addr, l, PCI_COMMAND))
        pci_update_irq_disabled(d, was_irq_disabled);
}

/***********************************************************/
/* generic PCI irq support */

/* 0 <= irq_num <= 3. level must be 0 or 1 */
static void pci_set_irq(void *opaque, int irq_num, int level)
{
    PCIDevice *pci_dev = opaque;
    int change;

    change = level - pci_irq_state(pci_dev, irq_num);
    if (!change)
        return;

    pci_set_irq_state(pci_dev, irq_num, level);
    pci_update_irq_status(pci_dev);
    if (pci_irq_disabled(pci_dev))
        return;
    pci_change_irq_level(pci_dev, irq_num, change);
}

/***********************************************************/
/* monitor info on PCI */

typedef struct {
    uint16_t class;
    const char *desc;
    const char *fw_name;
    uint16_t fw_ign_bits;
} pci_class_desc;

static const pci_class_desc pci_class_descriptions[] =
{
    { 0x0001, "VGA controller", "display"},
    { 0x0100, "SCSI controller", "scsi"},
    { 0x0101, "IDE controller", "ide"},
    { 0x0102, "Floppy controller", "fdc"},
    { 0x0103, "IPI controller", "ipi"},
    { 0x0104, "RAID controller", "raid"},
    { 0x0106, "SATA controller"},
    { 0x0107, "SAS controller"},
    { 0x0180, "Storage controller"},
    { 0x0200, "Ethernet controller", "ethernet"},
    { 0x0201, "Token Ring controller", "token-ring"},
    { 0x0202, "FDDI controller", "fddi"},
    { 0x0203, "ATM controller", "atm"},
    { 0x0280, "Network controller"},
    { 0x0300, "VGA controller", "display", 0x00ff},
    { 0x0301, "XGA controller"},
    { 0x0302, "3D controller"},
    { 0x0380, "Display controller"},
    { 0x0400, "Video controller", "video"},
    { 0x0401, "Audio controller", "sound"},
    { 0x0402, "Phone"},
    { 0x0403, "Audio controller", "sound"},
    { 0x0480, "Multimedia controller"},
    { 0x0500, "RAM controller", "memory"},
    { 0x0501, "Flash controller", "flash"},
    { 0x0580, "Memory controller"},
    { 0x0600, "Host bridge", "host"},
    { 0x0601, "ISA bridge", "isa"},
    { 0x0602, "EISA bridge", "eisa"},
    { 0x0603, "MC bridge", "mca"},
    { 0x0604, "PCI bridge", "pci"},
    { 0x0605, "PCMCIA bridge", "pcmcia"},
    { 0x0606, "NUBUS bridge", "nubus"},
    { 0x0607, "CARDBUS bridge", "cardbus"},
    { 0x0608, "RACEWAY bridge"},
    { 0x0680, "Bridge"},
    { 0x0700, "Serial port", "serial"},
    { 0x0701, "Parallel port", "parallel"},
    { 0x0800, "Interrupt controller", "interrupt-controller"},
    { 0x0801, "DMA controller", "dma-controller"},
    { 0x0802, "Timer", "timer"},
    { 0x0803, "RTC", "rtc"},
    { 0x0900, "Keyboard", "keyboard"},
    { 0x0901, "Pen", "pen"},
    { 0x0902, "Mouse", "mouse"},
    { 0x0A00, "Dock station", "dock", 0x00ff},
    { 0x0B00, "i386 cpu", "cpu", 0x00ff},
    { 0x0c00, "Fireware contorller", "fireware"},
    { 0x0c01, "Access bus controller", "access-bus"},
    { 0x0c02, "SSA controller", "ssa"},
    { 0x0c03, "USB controller", "usb"},
    { 0x0c04, "Fibre channel controller", "fibre-channel"},
    { 0, NULL}
};

static void pci_for_each_device_under_bus(PCIBus *bus,
                                          void (*fn)(PCIBus *b, PCIDevice *d))
{
    PCIDevice *d;
    int devfn;

    for(devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        d = bus->devices[devfn];
        if (d) {
            fn(bus, d);
        }
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

static void pci_device_print(Monitor *mon, QDict *device)
{
    QDict *qdict;
    QListEntry *entry;
    uint64_t addr, size;

    monitor_printf(mon, "  Bus %2" PRId64 ", ", qdict_get_int(device, "bus"));
    monitor_printf(mon, "device %3" PRId64 ", function %" PRId64 ":\n",
                        qdict_get_int(device, "slot"),
                        qdict_get_int(device, "function"));
    monitor_printf(mon, "    ");

    qdict = qdict_get_qdict(device, "class_info");
    if (qdict_haskey(qdict, "desc")) {
        monitor_printf(mon, "%s", qdict_get_str(qdict, "desc"));
    } else {
        monitor_printf(mon, "Class %04" PRId64, qdict_get_int(qdict, "class"));
    }

    qdict = qdict_get_qdict(device, "id");
    monitor_printf(mon, ": PCI device %04" PRIx64 ":%04" PRIx64 "\n",
                        qdict_get_int(qdict, "device"),
                        qdict_get_int(qdict, "vendor"));

    if (qdict_haskey(device, "irq")) {
        monitor_printf(mon, "      IRQ %" PRId64 ".\n",
                            qdict_get_int(device, "irq"));
    }

    if (qdict_haskey(device, "pci_bridge")) {
        QDict *info;

        qdict = qdict_get_qdict(device, "pci_bridge");

        info = qdict_get_qdict(qdict, "bus");
        monitor_printf(mon, "      BUS %" PRId64 ".\n",
                            qdict_get_int(info, "number"));
        monitor_printf(mon, "      secondary bus %" PRId64 ".\n",
                            qdict_get_int(info, "secondary"));
        monitor_printf(mon, "      subordinate bus %" PRId64 ".\n",
                            qdict_get_int(info, "subordinate"));

        info = qdict_get_qdict(qdict, "io_range");
        monitor_printf(mon, "      IO range [0x%04"PRIx64", 0x%04"PRIx64"]\n",
                       qdict_get_int(info, "base"),
                       qdict_get_int(info, "limit"));

        info = qdict_get_qdict(qdict, "memory_range");
        monitor_printf(mon,
                       "      memory range [0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       qdict_get_int(info, "base"),
                       qdict_get_int(info, "limit"));

        info = qdict_get_qdict(qdict, "prefetchable_range");
        monitor_printf(mon, "      prefetchable memory range "
                       "[0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       qdict_get_int(info, "base"),
        qdict_get_int(info, "limit"));
    }

    QLIST_FOREACH_ENTRY(qdict_get_qlist(device, "regions"), entry) {
        qdict = qobject_to_qdict(qlist_entry_obj(entry));
        monitor_printf(mon, "      BAR%d: ", (int) qdict_get_int(qdict, "bar"));

        addr = qdict_get_int(qdict, "address");
        size = qdict_get_int(qdict, "size");

        if (!strcmp(qdict_get_str(qdict, "type"), "io")) {
            monitor_printf(mon, "I/O at 0x%04"FMT_PCIBUS
                                " [0x%04"FMT_PCIBUS"].\n",
                                addr, addr + size - 1);
        } else {
            monitor_printf(mon, "%d bit%s memory at 0x%08"FMT_PCIBUS
                               " [0x%08"FMT_PCIBUS"].\n",
                                qdict_get_bool(qdict, "mem_type_64") ? 64 : 32,
                                qdict_get_bool(qdict, "prefetch") ?
                                " prefetchable" : "", addr, addr + size - 1);
        }
    }

    monitor_printf(mon, "      id \"%s\"\n", qdict_get_str(device, "qdev_id"));

    if (qdict_haskey(device, "pci_bridge")) {
        qdict = qdict_get_qdict(device, "pci_bridge");
        if (qdict_haskey(qdict, "devices")) {
            QListEntry *dev;
            QLIST_FOREACH_ENTRY(qdict_get_qlist(qdict, "devices"), dev) {
                pci_device_print(mon, qobject_to_qdict(qlist_entry_obj(dev)));
            }
        }
    }
}

void do_pci_info_print(Monitor *mon, const QObject *data)
{
    QListEntry *bus, *dev;

    QLIST_FOREACH_ENTRY(qobject_to_qlist(data), bus) {
        QDict *qdict = qobject_to_qdict(qlist_entry_obj(bus));
        QLIST_FOREACH_ENTRY(qdict_get_qlist(qdict, "devices"), dev) {
            pci_device_print(mon, qobject_to_qdict(qlist_entry_obj(dev)));
        }
    }
}

static QObject *pci_get_dev_class(const PCIDevice *dev)
{
    int class;
    const pci_class_desc *desc;

    class = pci_get_word(dev->config + PCI_CLASS_DEVICE);
    desc = pci_class_descriptions;
    while (desc->desc && class != desc->class)
        desc++;

    if (desc->desc) {
        return qobject_from_jsonf("{ 'desc': %s, 'class': %d }",
                                  desc->desc, class);
    } else {
        return qobject_from_jsonf("{ 'class': %d }", class);
    }
}

static QObject *pci_get_dev_id(const PCIDevice *dev)
{
    return qobject_from_jsonf("{ 'device': %d, 'vendor': %d }",
                              pci_get_word(dev->config + PCI_VENDOR_ID),
                              pci_get_word(dev->config + PCI_DEVICE_ID));
}

static QObject *pci_get_regions_list(const PCIDevice *dev)
{
    int i;
    QList *regions_list;

    regions_list = qlist_new();

    for (i = 0; i < PCI_NUM_REGIONS; i++) {
        QObject *obj;
        const PCIIORegion *r = &dev->io_regions[i];

        if (!r->size) {
            continue;
        }

        if (r->type & PCI_BASE_ADDRESS_SPACE_IO) {
            obj = qobject_from_jsonf("{ 'bar': %d, 'type': 'io', "
                                     "'address': %" PRId64 ", "
                                     "'size': %" PRId64 " }",
                                     i, r->addr, r->size);
        } else {
            int mem_type_64 = r->type & PCI_BASE_ADDRESS_MEM_TYPE_64;

            obj = qobject_from_jsonf("{ 'bar': %d, 'type': 'memory', "
                                     "'mem_type_64': %i, 'prefetch': %i, "
                                     "'address': %" PRId64 ", "
                                     "'size': %" PRId64 " }",
                                     i, mem_type_64,
                                     r->type & PCI_BASE_ADDRESS_MEM_PREFETCH,
                                     r->addr, r->size);
        }

        qlist_append_obj(regions_list, obj);
    }

    return QOBJECT(regions_list);
}

static QObject *pci_get_devices_list(PCIBus *bus, int bus_num);

static QObject *pci_get_dev_dict(PCIDevice *dev, PCIBus *bus, int bus_num)
{
    uint8_t type;
    QObject *obj;

    obj = qobject_from_jsonf("{ 'bus': %d, 'slot': %d, 'function': %d,"                                       "'class_info': %p, 'id': %p, 'regions': %p,"
                              " 'qdev_id': %s }",
                              bus_num,
                              PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
                              pci_get_dev_class(dev), pci_get_dev_id(dev),
                              pci_get_regions_list(dev),
                              dev->qdev.id ? dev->qdev.id : "");

    if (dev->config[PCI_INTERRUPT_PIN] != 0) {
        QDict *qdict = qobject_to_qdict(obj);
        qdict_put(qdict, "irq", qint_from_int(dev->config[PCI_INTERRUPT_LINE]));
    }

    type = dev->config[PCI_HEADER_TYPE] & ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    if (type == PCI_HEADER_TYPE_BRIDGE) {
        QDict *qdict;
        QObject *pci_bridge;

        pci_bridge = qobject_from_jsonf("{ 'bus': "
        "{ 'number': %d, 'secondary': %d, 'subordinate': %d }, "
        "'io_range': { 'base': %" PRId64 ", 'limit': %" PRId64 "}, "
        "'memory_range': { 'base': %" PRId64 ", 'limit': %" PRId64 "}, "
        "'prefetchable_range': { 'base': %" PRId64 ", 'limit': %" PRId64 "} }",
        dev->config[PCI_PRIMARY_BUS], dev->config[PCI_SECONDARY_BUS],
        dev->config[PCI_SUBORDINATE_BUS],
        pci_bridge_get_base(dev, PCI_BASE_ADDRESS_SPACE_IO),
        pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_SPACE_IO),
        pci_bridge_get_base(dev, PCI_BASE_ADDRESS_SPACE_MEMORY),
        pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_SPACE_MEMORY),
        pci_bridge_get_base(dev, PCI_BASE_ADDRESS_SPACE_MEMORY |
                               PCI_BASE_ADDRESS_MEM_PREFETCH),
        pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_SPACE_MEMORY |
                                PCI_BASE_ADDRESS_MEM_PREFETCH));

        if (dev->config[PCI_SECONDARY_BUS] != 0) {
            PCIBus *child_bus = pci_find_bus(bus, dev->config[PCI_SECONDARY_BUS]);

            if (child_bus) {
                qdict = qobject_to_qdict(pci_bridge);
                qdict_put_obj(qdict, "devices",
                              pci_get_devices_list(child_bus,
                                                   dev->config[PCI_SECONDARY_BUS]));
            }
        }
        qdict = qobject_to_qdict(obj);
        qdict_put_obj(qdict, "pci_bridge", pci_bridge);
    }

    return obj;
}

static QObject *pci_get_devices_list(PCIBus *bus, int bus_num)
{
    int devfn;
    PCIDevice *dev;
    QList *dev_list;

    dev_list = qlist_new();

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        dev = bus->devices[devfn];
        if (dev) {
            qlist_append_obj(dev_list, pci_get_dev_dict(dev, bus, bus_num));
        }
    }

    return QOBJECT(dev_list);
}

static QObject *pci_get_bus_dict(PCIBus *bus, int bus_num)
{
    bus = pci_find_bus(bus, bus_num);
    if (bus) {
        return qobject_from_jsonf("{ 'bus': %d, 'devices': %p }",
                                  bus_num, pci_get_devices_list(bus, bus_num));
    }

    return NULL;
}

void do_pci_info(Monitor *mon, QObject **ret_data)
{
    QList *bus_list;
    struct PCIHostBus *host;

    bus_list = qlist_new();

    QLIST_FOREACH(host, &host_buses, next) {
        QObject *obj = pci_get_bus_dict(host->bus, 0);
        if (obj) {
            qlist_append_obj(bus_list, obj);
        }
    }

    *ret_data = QOBJECT(bus_list);
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
        error_report("Invalid PCI device address %s for device %s",
                     devaddr, pci_nic_names[i]);
        return NULL;
    }

    pci_dev = pci_create(bus, devfn, pci_nic_names[i]);
    dev = &pci_dev->qdev;
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

static void pci_bridge_update_mappings_fn(PCIBus *b, PCIDevice *d)
{
    pci_update_mappings(d);
}

void pci_bridge_update_mappings(PCIBus *b)
{
    PCIBus *child;

    pci_for_each_device_under_bus(b, pci_bridge_update_mappings_fn);

    QLIST_FOREACH(child, &b->child, sibling) {
        pci_bridge_update_mappings(child);
    }
}

/* Whether a given bus number is in range of the secondary
 * bus of the given bridge device. */
static bool pci_secondary_bus_in_range(PCIDevice *dev, int bus_num)
{
    return !(pci_get_word(dev->config + PCI_BRIDGE_CONTROL) &
             PCI_BRIDGE_CTL_BUS_RESET) /* Don't walk the bus if it's reset. */ &&
        dev->config[PCI_SECONDARY_BUS] < bus_num &&
        bus_num <= dev->config[PCI_SUBORDINATE_BUS];
}

PCIBus *pci_find_bus(PCIBus *bus, int bus_num)
{
    PCIBus *sec;

    if (!bus) {
        return NULL;
    }

    if (pci_bus_num(bus) == bus_num) {
        return bus;
    }

    /* Consider all bus numbers in range for the host pci bridge. */
    if (bus->parent_dev &&
        !pci_secondary_bus_in_range(bus->parent_dev, bus_num)) {
        return NULL;
    }

    /* try child bus */
    for (; bus; bus = sec) {
        QLIST_FOREACH(sec, &bus->child, sibling) {
            assert(sec->parent_dev);
            if (sec->parent_dev->config[PCI_SECONDARY_BUS] == bus_num) {
                return sec;
            }
            if (pci_secondary_bus_in_range(sec->parent_dev, bus_num)) {
                break;
            }
        }
    }

    return NULL;
}

PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn)
{
    bus = pci_find_bus(bus, bus_num);

    if (!bus)
        return NULL;

    return bus->devices[devfn];
}

static int pci_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    PCIDeviceInfo *info = container_of(base, PCIDeviceInfo, qdev);
    PCIBus *bus;
    int devfn, rc;
    bool is_default_rom;

    /* initialize cap_present for pci_is_express() and pci_config_size() */
    if (info->is_express) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    bus = FROM_QBUS(PCIBus, qdev_get_parent_bus(qdev));
    devfn = pci_dev->devfn;
    pci_dev = do_pci_register_device(pci_dev, bus, base->name, devfn,
                                     info->config_read, info->config_write,
                                     info->is_bridge);
    if (pci_dev == NULL)
        return -1;
    if (qdev->hotplugged && info->no_hotplug) {
        qerror_report(QERR_DEVICE_NO_HOTPLUG, info->qdev.name);
        do_pci_unregister_device(pci_dev);
        return -1;
    }
    rc = info->init(pci_dev);
    if (rc != 0) {
        do_pci_unregister_device(pci_dev);
        return rc;
    }

    /* rom loading */
    is_default_rom = false;
    if (pci_dev->romfile == NULL && info->romfile != NULL) {
        pci_dev->romfile = qemu_strdup(info->romfile);
        is_default_rom = true;
    }
    pci_add_option_rom(pci_dev, is_default_rom);

    if (bus->hotplug) {
        /* Let buses differentiate between hotplug and when device is
         * enabled during qemu machine creation. */
        rc = bus->hotplug(bus->hotplug_qdev, pci_dev,
                          qdev->hotplugged ? PCI_HOTPLUG_ENABLED:
                          PCI_COLDPLUG_ENABLED);
        if (rc != 0) {
            int r = pci_unregister_device(&pci_dev->qdev);
            assert(!r);
            return rc;
        }
    }
    return 0;
}

static int pci_unplug_device(DeviceState *qdev)
{
    PCIDevice *dev = DO_UPCAST(PCIDevice, qdev, qdev);
    PCIDeviceInfo *info = container_of(qdev->info, PCIDeviceInfo, qdev);

    if (info->no_hotplug) {
        qerror_report(QERR_DEVICE_NO_HOTPLUG, info->qdev.name);
        return -1;
    }
    return dev->bus->hotplug(dev->bus->hotplug_qdev, dev,
                             PCI_HOTPLUG_DISABLED);
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

PCIDevice *pci_create_multifunction(PCIBus *bus, int devfn, bool multifunction,
                                    const char *name)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, name);
    qdev_prop_set_uint32(dev, "addr", devfn);
    qdev_prop_set_bit(dev, "multifunction", multifunction);
    return DO_UPCAST(PCIDevice, qdev, dev);
}

PCIDevice *pci_try_create_multifunction(PCIBus *bus, int devfn,
                                        bool multifunction,
                                        const char *name)
{
    DeviceState *dev;

    dev = qdev_try_create(&bus->qbus, name);
    if (!dev) {
        return NULL;
    }
    qdev_prop_set_uint32(dev, "addr", devfn);
    qdev_prop_set_bit(dev, "multifunction", multifunction);
    return DO_UPCAST(PCIDevice, qdev, dev);
}

PCIDevice *pci_create_simple_multifunction(PCIBus *bus, int devfn,
                                           bool multifunction,
                                           const char *name)
{
    PCIDevice *dev = pci_create_multifunction(bus, devfn, multifunction, name);
    qdev_init_nofail(&dev->qdev);
    return dev;
}

PCIDevice *pci_create(PCIBus *bus, int devfn, const char *name)
{
    return pci_create_multifunction(bus, devfn, false, name);
}

PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name)
{
    return pci_create_simple_multifunction(bus, devfn, false, name);
}

PCIDevice *pci_try_create(PCIBus *bus, int devfn, const char *name)
{
    return pci_try_create_multifunction(bus, devfn, false, name);
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

static void pci_map_option_rom(PCIDevice *pdev, int region_num, pcibus_t addr, pcibus_t size, int type)
{
    cpu_register_physical_memory(addr, size, pdev->rom_offset);
}

/* Patch the PCI vendor and device ids in a PCI rom image if necessary.
   This is needed for an option rom which is used for more than one device. */
static void pci_patch_ids(PCIDevice *pdev, uint8_t *ptr, int size)
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t rom_vendor_id;
    uint16_t rom_device_id;
    uint16_t rom_magic;
    uint16_t pcir_offset;
    uint8_t checksum;

    /* Words in rom data are little endian (like in PCI configuration),
       so they can be read / written with pci_get_word / pci_set_word. */

    /* Only a valid rom will be patched. */
    rom_magic = pci_get_word(ptr);
    if (rom_magic != 0xaa55) {
        PCI_DPRINTF("Bad ROM magic %04x\n", rom_magic);
        return;
    }
    pcir_offset = pci_get_word(ptr + 0x18);
    if (pcir_offset + 8 >= size || memcmp(ptr + pcir_offset, "PCIR", 4)) {
        PCI_DPRINTF("Bad PCIR offset 0x%x or signature\n", pcir_offset);
        return;
    }

    vendor_id = pci_get_word(pdev->config + PCI_VENDOR_ID);
    device_id = pci_get_word(pdev->config + PCI_DEVICE_ID);
    rom_vendor_id = pci_get_word(ptr + pcir_offset + 4);
    rom_device_id = pci_get_word(ptr + pcir_offset + 6);

    PCI_DPRINTF("%s: ROM id %04x%04x / PCI id %04x%04x\n", pdev->romfile,
                vendor_id, device_id, rom_vendor_id, rom_device_id);

    checksum = ptr[6];

    if (vendor_id != rom_vendor_id) {
        /* Patch vendor id and checksum (at offset 6 for etherboot roms). */
        checksum += (uint8_t)rom_vendor_id + (uint8_t)(rom_vendor_id >> 8);
        checksum -= (uint8_t)vendor_id + (uint8_t)(vendor_id >> 8);
        PCI_DPRINTF("ROM checksum %02x / %02x\n", ptr[6], checksum);
        ptr[6] = checksum;
        pci_set_word(ptr + pcir_offset + 4, vendor_id);
    }

    if (device_id != rom_device_id) {
        /* Patch device id and checksum (at offset 6 for etherboot roms). */
        checksum += (uint8_t)rom_device_id + (uint8_t)(rom_device_id >> 8);
        checksum -= (uint8_t)device_id + (uint8_t)(device_id >> 8);
        PCI_DPRINTF("ROM checksum %02x / %02x\n", ptr[6], checksum);
        ptr[6] = checksum;
        pci_set_word(ptr + pcir_offset + 6, device_id);
    }
}

/* Add an option rom for the device */
static int pci_add_option_rom(PCIDevice *pdev, bool is_default_rom)
{
    int size;
    char *path;
    void *ptr;
    char name[32];

    if (!pdev->romfile)
        return 0;
    if (strlen(pdev->romfile) == 0)
        return 0;

    if (!pdev->rom_bar) {
        /*
         * Load rom via fw_cfg instead of creating a rom bar,
         * for 0.11 compatibility.
         */
        int class = pci_get_word(pdev->config + PCI_CLASS_DEVICE);
        if (class == 0x0300) {
            rom_add_vga(pdev->romfile);
        } else {
            rom_add_option(pdev->romfile, -1);
        }
        return 0;
    }

    path = qemu_find_file(QEMU_FILE_TYPE_BIOS, pdev->romfile);
    if (path == NULL) {
        path = qemu_strdup(pdev->romfile);
    }

    size = get_image_size(path);
    if (size < 0) {
        error_report("%s: failed to find romfile \"%s\"",
                     __FUNCTION__, pdev->romfile);
        qemu_free(path);
        return -1;
    }
    if (size & (size - 1)) {
        size = 1 << qemu_fls(size);
    }

    if (pdev->qdev.info->vmsd)
        snprintf(name, sizeof(name), "%s.rom", pdev->qdev.info->vmsd->name);
    else
        snprintf(name, sizeof(name), "%s.rom", pdev->qdev.info->name);
    pdev->rom_offset = qemu_ram_alloc(&pdev->qdev, name, size);

    ptr = qemu_get_ram_ptr(pdev->rom_offset);
    load_image(path, ptr);
    qemu_free(path);

    if (is_default_rom) {
        /* Only the default rom images will be patched (if needed). */
        pci_patch_ids(pdev, ptr, size);
    }

    qemu_put_ram_ptr(ptr);

    pci_register_bar(pdev, PCI_ROM_SLOT, size,
                     0, pci_map_option_rom);

    return 0;
}

static void pci_del_option_rom(PCIDevice *pdev)
{
    if (!pdev->rom_offset)
        return;

    qemu_ram_free(pdev->rom_offset);
    pdev->rom_offset = 0;
}

/*
 * if !offset
 * Reserve space and add capability to the linked list in pci config space
 *
 * if offset = 0,
 * Find and reserve space and add capability to the linked list
 * in pci config space */
int pci_add_capability(PCIDevice *pdev, uint8_t cap_id,
                       uint8_t offset, uint8_t size)
{
    uint8_t *config;
    if (!offset) {
        offset = pci_find_space(pdev, size);
        if (!offset) {
            return -ENOSPC;
        }
    }

    config = pdev->config + offset;
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
    memset(pdev->w1cmask + offset, 0, size);
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
                   indent, "", ctxt, pci_bus_num(d->bus),
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

static char *pci_dev_fw_name(DeviceState *dev, char *buf, int len)
{
    PCIDevice *d = (PCIDevice *)dev;
    const char *name = NULL;
    const pci_class_desc *desc =  pci_class_descriptions;
    int class = pci_get_word(d->config + PCI_CLASS_DEVICE);

    while (desc->desc &&
          (class & ~desc->fw_ign_bits) !=
          (desc->class & ~desc->fw_ign_bits)) {
        desc++;
    }

    if (desc->desc) {
        name = desc->fw_name;
    }

    if (name) {
        pstrcpy(buf, len, name);
    } else {
        snprintf(buf, len, "pci%04x,%04x",
                 pci_get_word(d->config + PCI_VENDOR_ID),
                 pci_get_word(d->config + PCI_DEVICE_ID));
    }

    return buf;
}

static char *pcibus_get_fw_dev_path(DeviceState *dev)
{
    PCIDevice *d = (PCIDevice *)dev;
    char path[50], name[33];
    int off;

    off = snprintf(path, sizeof(path), "%s@%x",
                   pci_dev_fw_name(dev, name, sizeof name),
                   PCI_SLOT(d->devfn));
    if (PCI_FUNC(d->devfn))
        snprintf(path + off, sizeof(path) + off, ",%x", PCI_FUNC(d->devfn));
    return strdup(path);
}

static char *pcibus_get_dev_path(DeviceState *dev)
{
    PCIDevice *d = container_of(dev, PCIDevice, qdev);
    PCIDevice *t;
    int slot_depth;
    /* Path format: Domain:00:Slot.Function:Slot.Function....:Slot.Function.
     * 00 is added here to make this format compatible with
     * domain:Bus:Slot.Func for systems without nested PCI bridges.
     * Slot.Function list specifies the slot and function numbers for all
     * devices on the path from root to the specific device. */
    char domain[] = "DDDD:00";
    char slot[] = ":SS.F";
    int domain_len = sizeof domain - 1 /* For '\0' */;
    int slot_len = sizeof slot - 1 /* For '\0' */;
    int path_len;
    char *path, *p;
    int s;

    /* Calculate # of slots on path between device and root. */;
    slot_depth = 0;
    for (t = d; t; t = t->bus->parent_dev) {
        ++slot_depth;
    }

    path_len = domain_len + slot_len * slot_depth;

    /* Allocate memory, fill in the terminating null byte. */
    path = qemu_malloc(path_len + 1 /* For '\0' */);
    path[path_len] = '\0';

    /* First field is the domain. */
    s = snprintf(domain, sizeof domain, "%04x:00", pci_find_domain(d->bus));
    assert(s == domain_len);
    memcpy(path, domain, domain_len);

    /* Fill in slot numbers. We walk up from device to root, so need to print
     * them in the reverse order, last to first. */
    p = path + path_len;
    for (t = d; t; t = t->bus->parent_dev) {
        p -= slot_len;
        s = snprintf(slot, sizeof slot, ":%02x.%x",
                     PCI_SLOT(t->devfn), PCI_FUNC(t->devfn));
        assert(s == slot_len);
        memcpy(p, slot, slot_len);
    }

    return path;
}

static int pci_qdev_find_recursive(PCIBus *bus,
                                   const char *id, PCIDevice **pdev)
{
    DeviceState *qdev = qdev_find_recursive(&bus->qbus, id);
    if (!qdev) {
        return -ENODEV;
    }

    /* roughly check if given qdev is pci device */
    if (qdev->info->init == &pci_qdev_init &&
        qdev->parent_bus->info == &pci_bus_info) {
        *pdev = DO_UPCAST(PCIDevice, qdev, qdev);
        return 0;
    }
    return -EINVAL;
}

int pci_qdev_find_device(const char *id, PCIDevice **pdev)
{
    struct PCIHostBus *host;
    int rc = -ENODEV;

    QLIST_FOREACH(host, &host_buses, next) {
        int tmp = pci_qdev_find_recursive(host->bus, id, pdev);
        if (!tmp) {
            rc = 0;
            break;
        }
        if (tmp != -ENODEV) {
            rc = tmp;
        }
    }

    return rc;
}
