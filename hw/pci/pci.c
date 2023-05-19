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

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "sysemu/numa.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "qemu/range.h"
#include "trace.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/hotplug.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "pci-internal.h"

#include "hw/xen/xen.h"
#include "hw/i386/kvm/xen_evtchn.h"

//#define DEBUG_PCI
#ifdef DEBUG_PCI
# define PCI_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define PCI_DPRINTF(format, ...)       do { } while (0)
#endif

bool pci_available = true;

static char *pcibus_get_dev_path(DeviceState *dev);
static char *pcibus_get_fw_dev_path(DeviceState *dev);
static void pcibus_reset(BusState *qbus);

static Property pci_props[] = {
    DEFINE_PROP_PCI_DEVFN("addr", PCIDevice, devfn, -1),
    DEFINE_PROP_STRING("romfile", PCIDevice, romfile),
    DEFINE_PROP_UINT32("romsize", PCIDevice, romsize, -1),
    DEFINE_PROP_UINT32("rombar",  PCIDevice, rom_bar, 1),
    DEFINE_PROP_BIT("multifunction", PCIDevice, cap_present,
                    QEMU_PCI_CAP_MULTIFUNCTION_BITNR, false),
    DEFINE_PROP_BIT("x-pcie-lnksta-dllla", PCIDevice, cap_present,
                    QEMU_PCIE_LNKSTA_DLLLA_BITNR, true),
    DEFINE_PROP_BIT("x-pcie-extcap-init", PCIDevice, cap_present,
                    QEMU_PCIE_EXTCAP_INIT_BITNR, true),
    DEFINE_PROP_STRING("failover_pair_id", PCIDevice,
                       failover_pair_id),
    DEFINE_PROP_UINT32("acpi-index",  PCIDevice, acpi_index, 0),
    DEFINE_PROP_BIT("x-pcie-err-unc-mask", PCIDevice, cap_present,
                    QEMU_PCIE_ERR_UNC_MASK_BITNR, true),
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription vmstate_pcibus = {
    .name = "PCIBUS",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_EQUAL(nirq, PCIBus, NULL),
        VMSTATE_VARRAY_INT32(irq_count, PCIBus,
                             nirq, 0, vmstate_info_int32,
                             int32_t),
        VMSTATE_END_OF_LIST()
    }
};

static gint g_cmp_uint32(gconstpointer a, gconstpointer b, gpointer user_data)
{
    return a - b;
}

static GSequence *pci_acpi_index_list(void)
{
    static GSequence *used_acpi_index_list;

    if (!used_acpi_index_list) {
        used_acpi_index_list = g_sequence_new(NULL);
    }
    return used_acpi_index_list;
}

static void pci_init_bus_master(PCIDevice *pci_dev)
{
    AddressSpace *dma_as = pci_device_iommu_address_space(pci_dev);

    memory_region_init_alias(&pci_dev->bus_master_enable_region,
                             OBJECT(pci_dev), "bus master",
                             dma_as->root, 0, memory_region_size(dma_as->root));
    memory_region_set_enabled(&pci_dev->bus_master_enable_region, false);
    memory_region_add_subregion(&pci_dev->bus_master_container_region, 0,
                                &pci_dev->bus_master_enable_region);
}

static void pcibus_machine_done(Notifier *notifier, void *data)
{
    PCIBus *bus = container_of(notifier, PCIBus, machine_done);
    int i;

    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        if (bus->devices[i]) {
            pci_init_bus_master(bus->devices[i]);
        }
    }
}

static void pci_bus_realize(BusState *qbus, Error **errp)
{
    PCIBus *bus = PCI_BUS(qbus);

    bus->machine_done.notify = pcibus_machine_done;
    qemu_add_machine_init_done_notifier(&bus->machine_done);

    vmstate_register(NULL, VMSTATE_INSTANCE_ID_ANY, &vmstate_pcibus, bus);
}

static void pcie_bus_realize(BusState *qbus, Error **errp)
{
    PCIBus *bus = PCI_BUS(qbus);
    Error *local_err = NULL;

    pci_bus_realize(qbus, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * A PCI-E bus can support extended config space if it's the root
     * bus, or if the bus/bridge above it does as well
     */
    if (pci_bus_is_root(bus)) {
        bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;
    } else {
        PCIBus *parent_bus = pci_get_bus(bus->parent_dev);

        if (pci_bus_allows_extended_config_space(parent_bus)) {
            bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;
        }
    }
}

static void pci_bus_unrealize(BusState *qbus)
{
    PCIBus *bus = PCI_BUS(qbus);

    qemu_remove_machine_init_done_notifier(&bus->machine_done);

    vmstate_unregister(NULL, &vmstate_pcibus, bus);
}

static int pcibus_num(PCIBus *bus)
{
    if (pci_bus_is_root(bus)) {
        return 0; /* pci host bridge */
    }
    return bus->parent_dev->config[PCI_SECONDARY_BUS];
}

static uint16_t pcibus_numa_node(PCIBus *bus)
{
    return NUMA_NODE_UNASSIGNED;
}

static void pci_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    PCIBusClass *pbc = PCI_BUS_CLASS(klass);

    k->print_dev = pcibus_dev_print;
    k->get_dev_path = pcibus_get_dev_path;
    k->get_fw_dev_path = pcibus_get_fw_dev_path;
    k->realize = pci_bus_realize;
    k->unrealize = pci_bus_unrealize;
    k->reset = pcibus_reset;

    pbc->bus_num = pcibus_num;
    pbc->numa_node = pcibus_numa_node;
}

static const TypeInfo pci_bus_info = {
    .name = TYPE_PCI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(PCIBus),
    .class_size = sizeof(PCIBusClass),
    .class_init = pci_bus_class_init,
};

static const TypeInfo cxl_interface_info = {
    .name          = INTERFACE_CXL_DEVICE,
    .parent        = TYPE_INTERFACE,
};

static const TypeInfo pcie_interface_info = {
    .name          = INTERFACE_PCIE_DEVICE,
    .parent        = TYPE_INTERFACE,
};

static const TypeInfo conventional_pci_interface_info = {
    .name          = INTERFACE_CONVENTIONAL_PCI_DEVICE,
    .parent        = TYPE_INTERFACE,
};

static void pcie_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->realize = pcie_bus_realize;
}

static const TypeInfo pcie_bus_info = {
    .name = TYPE_PCIE_BUS,
    .parent = TYPE_PCI_BUS,
    .class_init = pcie_bus_class_init,
};

static const TypeInfo cxl_bus_info = {
    .name       = TYPE_CXL_BUS,
    .parent     = TYPE_PCIE_BUS,
    .class_init = pcie_bus_class_init,
};

static void pci_update_mappings(PCIDevice *d);
static void pci_irq_handler(void *opaque, int irq_num, int level);
static void pci_add_option_rom(PCIDevice *pdev, bool is_default_rom, Error **);
static void pci_del_option_rom(PCIDevice *pdev);

static uint16_t pci_default_sub_vendor_id = PCI_SUBVENDOR_ID_REDHAT_QUMRANET;
static uint16_t pci_default_sub_device_id = PCI_SUBDEVICE_ID_QEMU;

PCIHostStateList pci_host_bridges;

int pci_bar(PCIDevice *d, int reg)
{
    uint8_t type;

    /* PCIe virtual functions do not have their own BARs */
    assert(!pci_is_vf(d));

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

static void pci_bus_change_irq_level(PCIBus *bus, int irq_num, int change)
{
    assert(irq_num >= 0);
    assert(irq_num < bus->nirq);
    bus->irq_count[irq_num] += change;
    bus->set_irq(bus->irq_opaque, irq_num, bus->irq_count[irq_num] != 0);
}

static void pci_change_irq_level(PCIDevice *pci_dev, int irq_num, int change)
{
    PCIBus *bus;
    for (;;) {
        int dev_irq = irq_num;
        bus = pci_get_bus(pci_dev);
        assert(bus->map_irq);
        irq_num = bus->map_irq(pci_dev, irq_num);
        trace_pci_route_irq(dev_irq, DEVICE(pci_dev)->canonical_path, irq_num,
                            pci_bus_is_root(bus) ? "root-complex"
                                    : DEVICE(bus->parent_dev)->canonical_path);
        if (bus->set_irq)
            break;
        pci_dev = bus->parent_dev;
    }
    pci_bus_change_irq_level(bus, irq_num, change);
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
        pci_irq_handler(dev, i, 0);
    }
}

static void pci_msi_trigger(PCIDevice *dev, MSIMessage msg)
{
    MemTxAttrs attrs = {};

    /*
     * Xen uses the high bits of the address to contain some of the bits
     * of the PIRQ#. Therefore we can't just send the write cycle and
     * trust that it's caught by the APIC at 0xfee00000 because the
     * target of the write might be e.g. 0x0x1000fee46000 for PIRQ#4166.
     * So we intercept the delivery here instead of in kvm_send_msi().
     */
    if (xen_mode == XEN_EMULATE &&
        xen_evtchn_deliver_pirq_msi(msg.address, msg.data)) {
        return;
    }
    attrs.requester_id = pci_requester_id(dev);
    address_space_stl_le(&dev->bus_master_as, msg.address, msg.data,
                         attrs, NULL);
}

static void pci_reset_regions(PCIDevice *dev)
{
    int r;
    if (pci_is_vf(dev)) {
        return;
    }

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
}

static void pci_do_device_reset(PCIDevice *dev)
{
    pci_device_deassert_intx(dev);
    assert(dev->irq_state == 0);

    /* Clear all writable bits */
    pci_word_test_and_clear_mask(dev->config + PCI_COMMAND,
                                 pci_get_word(dev->wmask + PCI_COMMAND) |
                                 pci_get_word(dev->w1cmask + PCI_COMMAND));
    pci_word_test_and_clear_mask(dev->config + PCI_STATUS,
                                 pci_get_word(dev->wmask + PCI_STATUS) |
                                 pci_get_word(dev->w1cmask + PCI_STATUS));
    /* Some devices make bits of PCI_INTERRUPT_LINE read only */
    pci_byte_test_and_clear_mask(dev->config + PCI_INTERRUPT_LINE,
                              pci_get_word(dev->wmask + PCI_INTERRUPT_LINE) |
                              pci_get_word(dev->w1cmask + PCI_INTERRUPT_LINE));
    dev->config[PCI_CACHE_LINE_SIZE] = 0x0;
    pci_reset_regions(dev);
    pci_update_mappings(dev);

    msi_reset(dev);
    msix_reset(dev);
}

/*
 * This function is called on #RST and FLR.
 * FLR if PCI_EXP_DEVCTL_BCR_FLR is set
 */
void pci_device_reset(PCIDevice *dev)
{
    device_cold_reset(&dev->qdev);
    pci_do_device_reset(dev);
}

/*
 * Trigger pci bus reset under a given bus.
 * Called via bus_cold_reset on RST# assert, after the devices
 * have been reset device_cold_reset-ed already.
 */
static void pcibus_reset(BusState *qbus)
{
    PCIBus *bus = DO_UPCAST(PCIBus, qbus, qbus);
    int i;

    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        if (bus->devices[i]) {
            pci_do_device_reset(bus->devices[i]);
        }
    }

    for (i = 0; i < bus->nirq; i++) {
        assert(bus->irq_count[i] == 0);
    }
}

static void pci_host_bus_register(DeviceState *host)
{
    PCIHostState *host_bridge = PCI_HOST_BRIDGE(host);

    QLIST_INSERT_HEAD(&pci_host_bridges, host_bridge, next);
}

static void pci_host_bus_unregister(DeviceState *host)
{
    PCIHostState *host_bridge = PCI_HOST_BRIDGE(host);

    QLIST_REMOVE(host_bridge, next);
}

PCIBus *pci_device_root_bus(const PCIDevice *d)
{
    PCIBus *bus = pci_get_bus(d);

    while (!pci_bus_is_root(bus)) {
        d = bus->parent_dev;
        assert(d != NULL);

        bus = pci_get_bus(d);
    }

    return bus;
}

const char *pci_root_bus_path(PCIDevice *dev)
{
    PCIBus *rootbus = pci_device_root_bus(dev);
    PCIHostState *host_bridge = PCI_HOST_BRIDGE(rootbus->qbus.parent);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_GET_CLASS(host_bridge);

    assert(host_bridge->bus == rootbus);

    if (hc->root_bus_path) {
        return (*hc->root_bus_path)(host_bridge, rootbus);
    }

    return rootbus->qbus.name;
}

bool pci_bus_bypass_iommu(PCIBus *bus)
{
    PCIBus *rootbus = bus;
    PCIHostState *host_bridge;

    if (!pci_bus_is_root(bus)) {
        rootbus = pci_device_root_bus(bus->parent_dev);
    }

    host_bridge = PCI_HOST_BRIDGE(rootbus->qbus.parent);

    assert(host_bridge->bus == rootbus);

    return host_bridge->bypass_iommu;
}

static void pci_root_bus_internal_init(PCIBus *bus, DeviceState *parent,
                                       MemoryRegion *address_space_mem,
                                       MemoryRegion *address_space_io,
                                       uint8_t devfn_min)
{
    assert(PCI_FUNC(devfn_min) == 0);
    bus->devfn_min = devfn_min;
    bus->slot_reserved_mask = 0x0;
    bus->address_space_mem = address_space_mem;
    bus->address_space_io = address_space_io;
    bus->flags |= PCI_BUS_IS_ROOT;

    /* host bridge */
    QLIST_INIT(&bus->child);

    pci_host_bus_register(parent);
}

static void pci_bus_uninit(PCIBus *bus)
{
    pci_host_bus_unregister(BUS(bus)->parent);
}

bool pci_bus_is_express(const PCIBus *bus)
{
    return object_dynamic_cast(OBJECT(bus), TYPE_PCIE_BUS);
}

void pci_root_bus_init(PCIBus *bus, size_t bus_size, DeviceState *parent,
                       const char *name,
                       MemoryRegion *address_space_mem,
                       MemoryRegion *address_space_io,
                       uint8_t devfn_min, const char *typename)
{
    qbus_init(bus, bus_size, typename, parent, name);
    pci_root_bus_internal_init(bus, parent, address_space_mem,
                               address_space_io, devfn_min);
}

PCIBus *pci_root_bus_new(DeviceState *parent, const char *name,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io,
                         uint8_t devfn_min, const char *typename)
{
    PCIBus *bus;

    bus = PCI_BUS(qbus_new(typename, parent, name));
    pci_root_bus_internal_init(bus, parent, address_space_mem,
                               address_space_io, devfn_min);
    return bus;
}

void pci_root_bus_cleanup(PCIBus *bus)
{
    pci_bus_uninit(bus);
    /* the caller of the unplug hotplug handler will delete this device */
    qbus_unrealize(BUS(bus));
}

void pci_bus_irqs(PCIBus *bus, pci_set_irq_fn set_irq,
                  void *irq_opaque, int nirq)
{
    bus->set_irq = set_irq;
    bus->irq_opaque = irq_opaque;
    bus->nirq = nirq;
    bus->irq_count = g_malloc0(nirq * sizeof(bus->irq_count[0]));
}

void pci_bus_map_irqs(PCIBus *bus, pci_map_irq_fn map_irq)
{
    bus->map_irq = map_irq;
}

void pci_bus_irqs_cleanup(PCIBus *bus)
{
    bus->set_irq = NULL;
    bus->map_irq = NULL;
    bus->irq_opaque = NULL;
    bus->nirq = 0;
    g_free(bus->irq_count);
}

PCIBus *pci_register_root_bus(DeviceState *parent, const char *name,
                              pci_set_irq_fn set_irq, pci_map_irq_fn map_irq,
                              void *irq_opaque,
                              MemoryRegion *address_space_mem,
                              MemoryRegion *address_space_io,
                              uint8_t devfn_min, int nirq,
                              const char *typename)
{
    PCIBus *bus;

    bus = pci_root_bus_new(parent, name, address_space_mem,
                           address_space_io, devfn_min, typename);
    pci_bus_irqs(bus, set_irq, irq_opaque, nirq);
    pci_bus_map_irqs(bus, map_irq);
    return bus;
}

void pci_unregister_root_bus(PCIBus *bus)
{
    pci_bus_irqs_cleanup(bus);
    pci_root_bus_cleanup(bus);
}

int pci_bus_num(PCIBus *s)
{
    return PCI_BUS_GET_CLASS(s)->bus_num(s);
}

/* Returns the min and max bus numbers of a PCI bus hierarchy */
void pci_bus_range(PCIBus *bus, int *min_bus, int *max_bus)
{
    int i;
    *min_bus = *max_bus = pci_bus_num(bus);

    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        PCIDevice *dev = bus->devices[i];

        if (dev && IS_PCI_BRIDGE(dev)) {
            *min_bus = MIN(*min_bus, dev->config[PCI_SECONDARY_BUS]);
            *max_bus = MAX(*max_bus, dev->config[PCI_SUBORDINATE_BUS]);
        }
    }
}

int pci_bus_numa_node(PCIBus *bus)
{
    return PCI_BUS_GET_CLASS(bus)->numa_node(bus);
}

static int get_pci_config_device(QEMUFile *f, void *pv, size_t size,
                                 const VMStateField *field)
{
    PCIDevice *s = container_of(pv, PCIDevice, config);
    uint8_t *config;
    int i;

    assert(size == pci_config_size(s));
    config = g_malloc(size);

    qemu_get_buffer(f, config, size);
    for (i = 0; i < size; ++i) {
        if ((config[i] ^ s->config[i]) &
            s->cmask[i] & ~s->wmask[i] & ~s->w1cmask[i]) {
            error_report("%s: Bad config data: i=0x%x read: %x device: %x "
                         "cmask: %x wmask: %x w1cmask:%x", __func__,
                         i, config[i], s->config[i],
                         s->cmask[i], s->wmask[i], s->w1cmask[i]);
            g_free(config);
            return -EINVAL;
        }
    }
    memcpy(s->config, config, size);

    pci_update_mappings(s);
    if (IS_PCI_BRIDGE(s)) {
        pci_bridge_update_mappings(PCI_BRIDGE(s));
    }

    memory_region_set_enabled(&s->bus_master_enable_region,
                              pci_get_word(s->config + PCI_COMMAND)
                              & PCI_COMMAND_MASTER);

    g_free(config);
    return 0;
}

/* just put buffer */
static int put_pci_config_device(QEMUFile *f, void *pv, size_t size,
                                 const VMStateField *field, JSONWriter *vmdesc)
{
    const uint8_t **v = pv;
    assert(size == pci_config_size(container_of(pv, PCIDevice, config)));
    qemu_put_buffer(f, *v, size);

    return 0;
}

static VMStateInfo vmstate_info_pci_config = {
    .name = "pci config",
    .get  = get_pci_config_device,
    .put  = put_pci_config_device,
};

static int get_pci_irq_state(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field)
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

static int put_pci_irq_state(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field, JSONWriter *vmdesc)
{
    int i;
    PCIDevice *s = container_of(pv, PCIDevice, irq_state);

    for (i = 0; i < PCI_NUM_PINS; ++i) {
        qemu_put_be32(f, pci_irq_state(s, i));
    }

    return 0;
}

static VMStateInfo vmstate_info_pci_irq_state = {
    .name = "pci irq state",
    .get  = get_pci_irq_state,
    .put  = put_pci_irq_state,
};

static bool migrate_is_pcie(void *opaque, int version_id)
{
    return pci_is_express((PCIDevice *)opaque);
}

static bool migrate_is_not_pcie(void *opaque, int version_id)
{
    return !pci_is_express((PCIDevice *)opaque);
}

const VMStateDescription vmstate_pci_device = {
    .name = "PCIDevice",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32_POSITIVE_LE(version_id, PCIDevice),
        VMSTATE_BUFFER_UNSAFE_INFO_TEST(config, PCIDevice,
                                   migrate_is_not_pcie,
                                   0, vmstate_info_pci_config,
                                   PCI_CONFIG_SPACE_SIZE),
        VMSTATE_BUFFER_UNSAFE_INFO_TEST(config, PCIDevice,
                                   migrate_is_pcie,
                                   0, vmstate_info_pci_config,
                                   PCIE_CONFIG_SPACE_SIZE),
        VMSTATE_BUFFER_UNSAFE_INFO(irq_state, PCIDevice, 2,
                                   vmstate_info_pci_irq_state,
                                   PCI_NUM_PINS * sizeof(int32_t)),
        VMSTATE_END_OF_LIST()
    }
};


void pci_device_save(PCIDevice *s, QEMUFile *f)
{
    /* Clear interrupt status bit: it is implicit
     * in irq_state which we are saving.
     * This makes us compatible with old devices
     * which never set or clear this bit. */
    s->config[PCI_STATUS] &= ~PCI_STATUS_INTERRUPT;
    vmstate_save_state(f, &vmstate_pci_device, s, NULL);
    /* Restore the interrupt status bit. */
    pci_update_irq_status(s);
}

int pci_device_load(PCIDevice *s, QEMUFile *f)
{
    int ret;
    ret = vmstate_load_state(f, &vmstate_pci_device, s, s->version_id);
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
static int pci_parse_devaddr(const char *addr, int *domp, int *busp,
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

    *domp = dom;
    *busp = bus;
    *slotp = slot;
    if (funcp != NULL)
        *funcp = func;
    return 0;
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
    pci_word_test_and_set_mask(dev->wmask + PCI_COMMAND, PCI_COMMAND_SERR);

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

static void pci_init_mask_bridge(PCIDevice *d)
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

    /* Supported memory and i/o types */
    d->config[PCI_IO_BASE] |= PCI_IO_RANGE_TYPE_16;
    d->config[PCI_IO_LIMIT] |= PCI_IO_RANGE_TYPE_16;
    pci_word_test_and_set_mask(d->config + PCI_PREF_MEMORY_BASE,
                               PCI_PREF_RANGE_TYPE_64);
    pci_word_test_and_set_mask(d->config + PCI_PREF_MEMORY_LIMIT,
                               PCI_PREF_RANGE_TYPE_64);

    /*
     * TODO: Bridges default to 10-bit VGA decoding but we currently only
     * implement 16-bit decoding (no alias support).
     */
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
    d->cmask[PCI_IO_BASE] |= PCI_IO_RANGE_TYPE_MASK;
    d->cmask[PCI_IO_LIMIT] |= PCI_IO_RANGE_TYPE_MASK;
    pci_word_test_and_set_mask(d->cmask + PCI_PREF_MEMORY_BASE,
                               PCI_PREF_RANGE_TYPE_MASK);
    pci_word_test_and_set_mask(d->cmask + PCI_PREF_MEMORY_LIMIT,
                               PCI_PREF_RANGE_TYPE_MASK);
}

static void pci_init_multifunction(PCIBus *bus, PCIDevice *dev, Error **errp)
{
    uint8_t slot = PCI_SLOT(dev->devfn);
    uint8_t func;

    if (dev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        dev->config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
    }

    /*
     * With SR/IOV and ARI, a device at function 0 need not be a multifunction
     * device, as it may just be a VF that ended up with function 0 in
     * the legacy PCI interpretation. Avoid failing in such cases:
     */
    if (pci_is_vf(dev) &&
        dev->exp.sriov_vf.pf->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        return;
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
            error_setg(errp, "PCI: single function device can't be populated "
                       "in function %x.%x", slot, PCI_FUNC(dev->devfn));
            return;
        }
        return;
    }

    if (dev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        return;
    }
    /* function 0 indicates single function, so function > 0 must be NULL */
    for (func = 1; func < PCI_FUNC_MAX; ++func) {
        if (bus->devices[PCI_DEVFN(slot, func)]) {
            error_setg(errp, "PCI: %x.0 indicates single function, "
                       "but %x.%x is already populated.",
                       slot, slot, func);
            return;
        }
    }
}

static void pci_config_alloc(PCIDevice *pci_dev)
{
    int config_size = pci_config_size(pci_dev);

    pci_dev->config = g_malloc0(config_size);
    pci_dev->cmask = g_malloc0(config_size);
    pci_dev->wmask = g_malloc0(config_size);
    pci_dev->w1cmask = g_malloc0(config_size);
    pci_dev->used = g_malloc0(config_size);
}

static void pci_config_free(PCIDevice *pci_dev)
{
    g_free(pci_dev->config);
    g_free(pci_dev->cmask);
    g_free(pci_dev->wmask);
    g_free(pci_dev->w1cmask);
    g_free(pci_dev->used);
}

static void do_pci_unregister_device(PCIDevice *pci_dev)
{
    pci_get_bus(pci_dev)->devices[pci_dev->devfn] = NULL;
    pci_config_free(pci_dev);

    if (xen_mode == XEN_EMULATE) {
        xen_evtchn_remove_pci_device(pci_dev);
    }
    if (memory_region_is_mapped(&pci_dev->bus_master_enable_region)) {
        memory_region_del_subregion(&pci_dev->bus_master_container_region,
                                    &pci_dev->bus_master_enable_region);
    }
    address_space_destroy(&pci_dev->bus_master_as);
}

/* Extract PCIReqIDCache into BDF format */
static uint16_t pci_req_id_cache_extract(PCIReqIDCache *cache)
{
    uint8_t bus_n;
    uint16_t result;

    switch (cache->type) {
    case PCI_REQ_ID_BDF:
        result = pci_get_bdf(cache->dev);
        break;
    case PCI_REQ_ID_SECONDARY_BUS:
        bus_n = pci_dev_bus_num(cache->dev);
        result = PCI_BUILD_BDF(bus_n, 0);
        break;
    default:
        error_report("Invalid PCI requester ID cache type: %d",
                     cache->type);
        exit(1);
        break;
    }

    return result;
}

/* Parse bridges up to the root complex and return requester ID
 * cache for specific device.  For full PCIe topology, the cache
 * result would be exactly the same as getting BDF of the device.
 * However, several tricks are required when system mixed up with
 * legacy PCI devices and PCIe-to-PCI bridges.
 *
 * Here we cache the proxy device (and type) not requester ID since
 * bus number might change from time to time.
 */
static PCIReqIDCache pci_req_id_cache_get(PCIDevice *dev)
{
    PCIDevice *parent;
    PCIReqIDCache cache = {
        .dev = dev,
        .type = PCI_REQ_ID_BDF,
    };

    while (!pci_bus_is_root(pci_get_bus(dev))) {
        /* We are under PCI/PCIe bridges */
        parent = pci_get_bus(dev)->parent_dev;
        if (pci_is_express(parent)) {
            if (pcie_cap_get_type(parent) == PCI_EXP_TYPE_PCI_BRIDGE) {
                /* When we pass through PCIe-to-PCI/PCIX bridges, we
                 * override the requester ID using secondary bus
                 * number of parent bridge with zeroed devfn
                 * (pcie-to-pci bridge spec chap 2.3). */
                cache.type = PCI_REQ_ID_SECONDARY_BUS;
                cache.dev = dev;
            }
        } else {
            /* Legacy PCI, override requester ID with the bridge's
             * BDF upstream.  When the root complex connects to
             * legacy PCI devices (including buses), it can only
             * obtain requester ID info from directly attached
             * devices.  If devices are attached under bridges, only
             * the requester ID of the bridge that is directly
             * attached to the root complex can be recognized. */
            cache.type = PCI_REQ_ID_BDF;
            cache.dev = parent;
        }
        dev = parent;
    }

    return cache;
}

uint16_t pci_requester_id(PCIDevice *dev)
{
    return pci_req_id_cache_extract(&dev->requester_id_cache);
}

static bool pci_bus_devfn_available(PCIBus *bus, int devfn)
{
    return !(bus->devices[devfn]);
}

static bool pci_bus_devfn_reserved(PCIBus *bus, int devfn)
{
    return bus->slot_reserved_mask & (1UL << PCI_SLOT(devfn));
}

uint32_t pci_bus_get_slot_reserved_mask(PCIBus *bus)
{
    return bus->slot_reserved_mask;
}

void pci_bus_set_slot_reserved_mask(PCIBus *bus, uint32_t mask)
{
    bus->slot_reserved_mask |= mask;
}

void pci_bus_clear_slot_reserved_mask(PCIBus *bus, uint32_t mask)
{
    bus->slot_reserved_mask &= ~mask;
}

/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev,
                                         const char *name, int devfn,
                                         Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    PCIConfigReadFunc *config_read = pc->config_read;
    PCIConfigWriteFunc *config_write = pc->config_write;
    Error *local_err = NULL;
    DeviceState *dev = DEVICE(pci_dev);
    PCIBus *bus = pci_get_bus(pci_dev);
    bool is_bridge = IS_PCI_BRIDGE(pci_dev);

    /* Only pci bridges can be attached to extra PCI root buses */
    if (pci_bus_is_root(bus) && bus->parent_dev && !is_bridge) {
        error_setg(errp,
                   "PCI: Only PCI/PCIe bridges can be plugged into %s",
                    bus->parent_dev->name);
        return NULL;
    }

    if (devfn < 0) {
        for(devfn = bus->devfn_min ; devfn < ARRAY_SIZE(bus->devices);
            devfn += PCI_FUNC_MAX) {
            if (pci_bus_devfn_available(bus, devfn) &&
                   !pci_bus_devfn_reserved(bus, devfn)) {
                goto found;
            }
        }
        error_setg(errp, "PCI: no slot/function available for %s, all in use "
                   "or reserved", name);
        return NULL;
    found: ;
    } else if (pci_bus_devfn_reserved(bus, devfn)) {
        error_setg(errp, "PCI: slot %d function %d not available for %s,"
                   " reserved",
                   PCI_SLOT(devfn), PCI_FUNC(devfn), name);
        return NULL;
    } else if (!pci_bus_devfn_available(bus, devfn)) {
        error_setg(errp, "PCI: slot %d function %d not available for %s,"
                   " in use by %s,id=%s",
                   PCI_SLOT(devfn), PCI_FUNC(devfn), name,
                   bus->devices[devfn]->name, bus->devices[devfn]->qdev.id);
        return NULL;
    } else if (dev->hotplugged &&
               !pci_is_vf(pci_dev) &&
               pci_get_function_0(pci_dev)) {
        error_setg(errp, "PCI: slot %d function 0 already occupied by %s,"
                   " new func %s cannot be exposed to guest.",
                   PCI_SLOT(pci_get_function_0(pci_dev)->devfn),
                   pci_get_function_0(pci_dev)->name,
                   name);

       return NULL;
    }

    pci_dev->devfn = devfn;
    pci_dev->requester_id_cache = pci_req_id_cache_get(pci_dev);
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);

    memory_region_init(&pci_dev->bus_master_container_region, OBJECT(pci_dev),
                       "bus master container", UINT64_MAX);
    address_space_init(&pci_dev->bus_master_as,
                       &pci_dev->bus_master_container_region, pci_dev->name);

    if (phase_check(PHASE_MACHINE_READY)) {
        pci_init_bus_master(pci_dev);
    }
    pci_dev->irq_state = 0;
    pci_config_alloc(pci_dev);

    pci_config_set_vendor_id(pci_dev->config, pc->vendor_id);
    pci_config_set_device_id(pci_dev->config, pc->device_id);
    pci_config_set_revision(pci_dev->config, pc->revision);
    pci_config_set_class(pci_dev->config, pc->class_id);

    if (!is_bridge) {
        if (pc->subsystem_vendor_id || pc->subsystem_id) {
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID,
                         pc->subsystem_vendor_id);
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID,
                         pc->subsystem_id);
        } else {
            pci_set_default_subsystem_id(pci_dev);
        }
    } else {
        /* subsystem_vendor_id/subsystem_id are only for header type 0 */
        assert(!pc->subsystem_vendor_id);
        assert(!pc->subsystem_id);
    }
    pci_init_cmask(pci_dev);
    pci_init_wmask(pci_dev);
    pci_init_w1cmask(pci_dev);
    if (is_bridge) {
        pci_init_mask_bridge(pci_dev);
    }
    pci_init_multifunction(bus, pci_dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        do_pci_unregister_device(pci_dev);
        return NULL;
    }

    if (!config_read)
        config_read = pci_default_read_config;
    if (!config_write)
        config_write = pci_default_write_config;
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    bus->devices[devfn] = pci_dev;
    pci_dev->version_id = 2; /* Current pci device vmstate version */
    return pci_dev;
}

static void pci_unregister_io_regions(PCIDevice *pci_dev)
{
    PCIIORegion *r;
    int i;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &pci_dev->io_regions[i];
        if (!r->size || r->addr == PCI_BAR_UNMAPPED)
            continue;
        memory_region_del_subregion(r->address_space, r->memory);
    }

    pci_unregister_vga(pci_dev);
}

static void pci_qdev_unrealize(DeviceState *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);

    pci_unregister_io_regions(pci_dev);
    pci_del_option_rom(pci_dev);

    if (pc->exit) {
        pc->exit(pci_dev);
    }

    pci_device_deassert_intx(pci_dev);
    do_pci_unregister_device(pci_dev);

    pci_dev->msi_trigger = NULL;

    /*
     * clean up acpi-index so it could reused by another device
     */
    if (pci_dev->acpi_index) {
        GSequence *used_indexes = pci_acpi_index_list();

        g_sequence_remove(g_sequence_lookup(used_indexes,
                          GINT_TO_POINTER(pci_dev->acpi_index),
                          g_cmp_uint32, NULL));
    }
}

void pci_register_bar(PCIDevice *pci_dev, int region_num,
                      uint8_t type, MemoryRegion *memory)
{
    PCIIORegion *r;
    uint32_t addr; /* offset in pci config space */
    uint64_t wmask;
    pcibus_t size = memory_region_size(memory);
    uint8_t hdr_type;

    assert(!pci_is_vf(pci_dev)); /* VFs must use pcie_sriov_vf_register_bar */
    assert(region_num >= 0);
    assert(region_num < PCI_NUM_REGIONS);
    assert(is_power_of_2(size));

    /* A PCI bridge device (with Type 1 header) may only have at most 2 BARs */
    hdr_type =
        pci_dev->config[PCI_HEADER_TYPE] & ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    assert(hdr_type != PCI_HEADER_TYPE_BRIDGE || region_num < 2);

    r = &pci_dev->io_regions[region_num];
    r->addr = PCI_BAR_UNMAPPED;
    r->size = size;
    r->type = type;
    r->memory = memory;
    r->address_space = type & PCI_BASE_ADDRESS_SPACE_IO
                        ? pci_get_bus(pci_dev)->address_space_io
                        : pci_get_bus(pci_dev)->address_space_mem;

    wmask = ~(size - 1);
    if (region_num == PCI_ROM_SLOT) {
        /* ROM enable bit is writable */
        wmask |= PCI_ROM_ADDRESS_ENABLE;
    }

    addr = pci_bar(pci_dev, region_num);
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

static void pci_update_vga(PCIDevice *pci_dev)
{
    uint16_t cmd;

    if (!pci_dev->has_vga) {
        return;
    }

    cmd = pci_get_word(pci_dev->config + PCI_COMMAND);

    memory_region_set_enabled(pci_dev->vga_regions[QEMU_PCI_VGA_MEM],
                              cmd & PCI_COMMAND_MEMORY);
    memory_region_set_enabled(pci_dev->vga_regions[QEMU_PCI_VGA_IO_LO],
                              cmd & PCI_COMMAND_IO);
    memory_region_set_enabled(pci_dev->vga_regions[QEMU_PCI_VGA_IO_HI],
                              cmd & PCI_COMMAND_IO);
}

void pci_register_vga(PCIDevice *pci_dev, MemoryRegion *mem,
                      MemoryRegion *io_lo, MemoryRegion *io_hi)
{
    PCIBus *bus = pci_get_bus(pci_dev);

    assert(!pci_dev->has_vga);

    assert(memory_region_size(mem) == QEMU_PCI_VGA_MEM_SIZE);
    pci_dev->vga_regions[QEMU_PCI_VGA_MEM] = mem;
    memory_region_add_subregion_overlap(bus->address_space_mem,
                                        QEMU_PCI_VGA_MEM_BASE, mem, 1);

    assert(memory_region_size(io_lo) == QEMU_PCI_VGA_IO_LO_SIZE);
    pci_dev->vga_regions[QEMU_PCI_VGA_IO_LO] = io_lo;
    memory_region_add_subregion_overlap(bus->address_space_io,
                                        QEMU_PCI_VGA_IO_LO_BASE, io_lo, 1);

    assert(memory_region_size(io_hi) == QEMU_PCI_VGA_IO_HI_SIZE);
    pci_dev->vga_regions[QEMU_PCI_VGA_IO_HI] = io_hi;
    memory_region_add_subregion_overlap(bus->address_space_io,
                                        QEMU_PCI_VGA_IO_HI_BASE, io_hi, 1);
    pci_dev->has_vga = true;

    pci_update_vga(pci_dev);
}

void pci_unregister_vga(PCIDevice *pci_dev)
{
    PCIBus *bus = pci_get_bus(pci_dev);

    if (!pci_dev->has_vga) {
        return;
    }

    memory_region_del_subregion(bus->address_space_mem,
                                pci_dev->vga_regions[QEMU_PCI_VGA_MEM]);
    memory_region_del_subregion(bus->address_space_io,
                                pci_dev->vga_regions[QEMU_PCI_VGA_IO_LO]);
    memory_region_del_subregion(bus->address_space_io,
                                pci_dev->vga_regions[QEMU_PCI_VGA_IO_HI]);
    pci_dev->has_vga = false;
}

pcibus_t pci_get_bar_addr(PCIDevice *pci_dev, int region_num)
{
    return pci_dev->io_regions[region_num].addr;
}

static pcibus_t pci_config_get_bar_addr(PCIDevice *d, int reg,
                                        uint8_t type, pcibus_t size)
{
    pcibus_t new_addr;
    if (!pci_is_vf(d)) {
        int bar = pci_bar(d, reg);
        if (type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
            new_addr = pci_get_quad(d->config + bar);
        } else {
            new_addr = pci_get_long(d->config + bar);
        }
    } else {
        PCIDevice *pf = d->exp.sriov_vf.pf;
        uint16_t sriov_cap = pf->exp.sriov_cap;
        int bar = sriov_cap + PCI_SRIOV_BAR + reg * 4;
        uint16_t vf_offset =
            pci_get_word(pf->config + sriov_cap + PCI_SRIOV_VF_OFFSET);
        uint16_t vf_stride =
            pci_get_word(pf->config + sriov_cap + PCI_SRIOV_VF_STRIDE);
        uint32_t vf_num = (d->devfn - (pf->devfn + vf_offset)) / vf_stride;

        if (type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
            new_addr = pci_get_quad(pf->config + bar);
        } else {
            new_addr = pci_get_long(pf->config + bar);
        }
        new_addr += vf_num * size;
    }
    /* The ROM slot has a specific enable bit, keep it intact */
    if (reg != PCI_ROM_SLOT) {
        new_addr &= ~(size - 1);
    }
    return new_addr;
}

pcibus_t pci_bar_address(PCIDevice *d,
                         int reg, uint8_t type, pcibus_t size)
{
    pcibus_t new_addr, last_addr;
    uint16_t cmd = pci_get_word(d->config + PCI_COMMAND);
    Object *machine = qdev_get_machine();
    ObjectClass *oc = object_get_class(machine);
    MachineClass *mc = MACHINE_CLASS(oc);
    bool allow_0_address = mc->pci_allow_0_address;

    if (type & PCI_BASE_ADDRESS_SPACE_IO) {
        if (!(cmd & PCI_COMMAND_IO)) {
            return PCI_BAR_UNMAPPED;
        }
        new_addr = pci_config_get_bar_addr(d, reg, type, size);
        last_addr = new_addr + size - 1;
        /* Check if 32 bit BAR wraps around explicitly.
         * TODO: make priorities correct and remove this work around.
         */
        if (last_addr <= new_addr || last_addr >= UINT32_MAX ||
            (!allow_0_address && new_addr == 0)) {
            return PCI_BAR_UNMAPPED;
        }
        return new_addr;
    }

    if (!(cmd & PCI_COMMAND_MEMORY)) {
        return PCI_BAR_UNMAPPED;
    }
    new_addr = pci_config_get_bar_addr(d, reg, type, size);
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
    if (last_addr <= new_addr || last_addr == PCI_BAR_UNMAPPED ||
        (!allow_0_address && new_addr == 0)) {
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
    if (last_addr >= HWADDR_MAX) {
        return PCI_BAR_UNMAPPED;
    }

    return new_addr;
}

static void pci_update_mappings(PCIDevice *d)
{
    PCIIORegion *r;
    int i;
    pcibus_t new_addr;

    for(i = 0; i < PCI_NUM_REGIONS; i++) {
        r = &d->io_regions[i];

        /* this region isn't registered */
        if (!r->size)
            continue;

        new_addr = pci_bar_address(d, i, r->type, r->size);
        if (!d->has_power) {
            new_addr = PCI_BAR_UNMAPPED;
        }

        /* This bar isn't changed */
        if (new_addr == r->addr)
            continue;

        /* now do the real mapping */
        if (r->addr != PCI_BAR_UNMAPPED) {
            trace_pci_update_mappings_del(d->name, pci_dev_bus_num(d),
                                          PCI_SLOT(d->devfn),
                                          PCI_FUNC(d->devfn),
                                          i, r->addr, r->size);
            memory_region_del_subregion(r->address_space, r->memory);
        }
        r->addr = new_addr;
        if (r->addr != PCI_BAR_UNMAPPED) {
            trace_pci_update_mappings_add(d->name, pci_dev_bus_num(d),
                                          PCI_SLOT(d->devfn),
                                          PCI_FUNC(d->devfn),
                                          i, r->addr, r->size);
            memory_region_add_subregion_overlap(r->address_space,
                                                r->addr, r->memory, 1);
        }
    }

    pci_update_vga(d);
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

    assert(address + len <= pci_config_size(d));

    if (pci_is_express_downstream_port(d) &&
        ranges_overlap(address, len, d->exp.exp_cap + PCI_EXP_LNKSTA, 2)) {
        pcie_sync_bridge_lnk(d);
    }
    memcpy(&val, d->config + address, len);
    return le32_to_cpu(val);
}

void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val_in, int l)
{
    int i, was_irq_disabled = pci_irq_disabled(d);
    uint32_t val = val_in;

    assert(addr + l <= pci_config_size(d));

    for (i = 0; i < l; val >>= 8, ++i) {
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

    if (range_covers_byte(addr, l, PCI_COMMAND)) {
        pci_update_irq_disabled(d, was_irq_disabled);
        memory_region_set_enabled(&d->bus_master_enable_region,
                                  (pci_get_word(d->config + PCI_COMMAND)
                                   & PCI_COMMAND_MASTER) && d->has_power);
    }

    msi_write_config(d, addr, val_in, l);
    msix_write_config(d, addr, val_in, l);
    pcie_sriov_config_write(d, addr, val_in, l);
}

/***********************************************************/
/* generic PCI irq support */

/* 0 <= irq_num <= 3. level must be 0 or 1 */
static void pci_irq_handler(void *opaque, int irq_num, int level)
{
    PCIDevice *pci_dev = opaque;
    int change;

    assert(0 <= irq_num && irq_num < PCI_NUM_PINS);
    assert(level == 0 || level == 1);
    change = level - pci_irq_state(pci_dev, irq_num);
    if (!change)
        return;

    pci_set_irq_state(pci_dev, irq_num, level);
    pci_update_irq_status(pci_dev);
    if (pci_irq_disabled(pci_dev))
        return;
    pci_change_irq_level(pci_dev, irq_num, change);
}

qemu_irq pci_allocate_irq(PCIDevice *pci_dev)
{
    int intx = pci_intx(pci_dev);
    assert(0 <= intx && intx < PCI_NUM_PINS);

    return qemu_allocate_irq(pci_irq_handler, pci_dev, intx);
}

void pci_set_irq(PCIDevice *pci_dev, int level)
{
    int intx = pci_intx(pci_dev);
    pci_irq_handler(pci_dev, intx, level);
}

/* Special hooks used by device assignment */
void pci_bus_set_route_irq_fn(PCIBus *bus, pci_route_irq_fn route_intx_to_irq)
{
    assert(pci_bus_is_root(bus));
    bus->route_intx_to_irq = route_intx_to_irq;
}

PCIINTxRoute pci_device_route_intx_to_irq(PCIDevice *dev, int pin)
{
    PCIBus *bus;

    do {
        int dev_irq = pin;
        bus = pci_get_bus(dev);
        pin = bus->map_irq(dev, pin);
        trace_pci_route_irq(dev_irq, DEVICE(dev)->canonical_path, pin,
                            pci_bus_is_root(bus) ? "root-complex"
                                    : DEVICE(bus->parent_dev)->canonical_path);
        dev = bus->parent_dev;
    } while (dev);

    if (!bus->route_intx_to_irq) {
        error_report("PCI: Bug - unimplemented PCI INTx routing (%s)",
                     object_get_typename(OBJECT(bus->qbus.parent)));
        return (PCIINTxRoute) { PCI_INTX_DISABLED, -1 };
    }

    return bus->route_intx_to_irq(bus->irq_opaque, pin);
}

bool pci_intx_route_changed(PCIINTxRoute *old, PCIINTxRoute *new)
{
    return old->mode != new->mode || old->irq != new->irq;
}

void pci_bus_fire_intx_routing_notifier(PCIBus *bus)
{
    PCIDevice *dev;
    PCIBus *sec;
    int i;

    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        dev = bus->devices[i];
        if (dev && dev->intx_routing_notifier) {
            dev->intx_routing_notifier(dev);
        }
    }

    QLIST_FOREACH(sec, &bus->child, sibling) {
        pci_bus_fire_intx_routing_notifier(sec);
    }
}

void pci_device_set_intx_routing_notifier(PCIDevice *dev,
                                          PCIINTxRoutingNotifier notifier)
{
    dev->intx_routing_notifier = notifier;
}

/*
 * PCI-to-PCI bridge specification
 * 9.1: Interrupt routing. Table 9-1
 *
 * the PCI Express Base Specification, Revision 2.1
 * 2.2.8.1: INTx interrupt signaling - Rules
 *          the Implementation Note
 *          Table 2-20
 */
/*
 * 0 <= pin <= 3 0 = INTA, 1 = INTB, 2 = INTC, 3 = INTD
 * 0-origin unlike PCI interrupt pin register.
 */
int pci_swizzle_map_irq_fn(PCIDevice *pci_dev, int pin)
{
    return pci_swizzle(PCI_SLOT(pci_dev->devfn), pin);
}

/***********************************************************/
/* monitor info on PCI */

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
    { 0x0604, "PCI bridge", "pci-bridge"},
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
    { 0x0c00, "Firewire controller", "firewire"},
    { 0x0c01, "Access bus controller", "access-bus"},
    { 0x0c02, "SSA controller", "ssa"},
    { 0x0c03, "USB controller", "usb"},
    { 0x0c04, "Fibre channel controller", "fibre-channel"},
    { 0x0c05, "SMBus"},
    { 0, NULL}
};

void pci_for_each_device_under_bus_reverse(PCIBus *bus,
                                           pci_bus_dev_fn fn,
                                           void *opaque)
{
    PCIDevice *d;
    int devfn;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        d = bus->devices[ARRAY_SIZE(bus->devices) - 1 - devfn];
        if (d) {
            fn(bus, d, opaque);
        }
    }
}

void pci_for_each_device_reverse(PCIBus *bus, int bus_num,
                                 pci_bus_dev_fn fn, void *opaque)
{
    bus = pci_find_bus_nr(bus, bus_num);

    if (bus) {
        pci_for_each_device_under_bus_reverse(bus, fn, opaque);
    }
}

void pci_for_each_device_under_bus(PCIBus *bus,
                                   pci_bus_dev_fn fn, void *opaque)
{
    PCIDevice *d;
    int devfn;

    for(devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        d = bus->devices[devfn];
        if (d) {
            fn(bus, d, opaque);
        }
    }
}

void pci_for_each_device(PCIBus *bus, int bus_num,
                         pci_bus_dev_fn fn, void *opaque)
{
    bus = pci_find_bus_nr(bus, bus_num);

    if (bus) {
        pci_for_each_device_under_bus(bus, fn, opaque);
    }
}

const pci_class_desc *get_class_desc(int class)
{
    const pci_class_desc *desc;

    desc = pci_class_descriptions;
    while (desc->desc && class != desc->class) {
        desc++;
    }

    return desc;
}

/* Initialize a PCI NIC.  */
PCIDevice *pci_nic_init_nofail(NICInfo *nd, PCIBus *rootbus,
                               const char *default_model,
                               const char *default_devaddr)
{
    const char *devaddr = nd->devaddr ? nd->devaddr : default_devaddr;
    GPtrArray *pci_nic_models;
    PCIBus *bus;
    PCIDevice *pci_dev;
    DeviceState *dev;
    int devfn;
    int i;
    int dom, busnr;
    unsigned slot;

    if (nd->model && !strcmp(nd->model, "virtio")) {
        g_free(nd->model);
        nd->model = g_strdup("virtio-net-pci");
    }

    pci_nic_models = qemu_get_nic_models(TYPE_PCI_DEVICE);

    if (qemu_show_nic_models(nd->model, (const char **)pci_nic_models->pdata)) {
        exit(0);
    }

    i = qemu_find_nic_model(nd, (const char **)pci_nic_models->pdata,
                            default_model);
    if (i < 0) {
        exit(1);
    }

    if (!rootbus) {
        error_report("No primary PCI bus");
        exit(1);
    }

    assert(!rootbus->parent_dev);

    if (!devaddr) {
        devfn = -1;
        busnr = 0;
    } else {
        if (pci_parse_devaddr(devaddr, &dom, &busnr, &slot, NULL) < 0) {
            error_report("Invalid PCI device address %s for device %s",
                         devaddr, nd->model);
            exit(1);
        }

        if (dom != 0) {
            error_report("No support for non-zero PCI domains");
            exit(1);
        }

        devfn = PCI_DEVFN(slot, 0);
    }

    bus = pci_find_bus_nr(rootbus, busnr);
    if (!bus) {
        error_report("Invalid PCI device address %s for device %s",
                     devaddr, nd->model);
        exit(1);
    }

    pci_dev = pci_new(devfn, nd->model);
    dev = &pci_dev->qdev;
    qdev_set_nic_properties(dev, nd);
    pci_realize_and_unref(pci_dev, bus, &error_fatal);
    g_ptr_array_free(pci_nic_models, true);
    return pci_dev;
}

PCIDevice *pci_vga_init(PCIBus *bus)
{
    vga_interface_created = true;
    switch (vga_interface_type) {
    case VGA_CIRRUS:
        return pci_create_simple(bus, -1, "cirrus-vga");
    case VGA_QXL:
        return pci_create_simple(bus, -1, "qxl-vga");
    case VGA_STD:
        return pci_create_simple(bus, -1, "VGA");
    case VGA_VMWARE:
        return pci_create_simple(bus, -1, "vmware-svga");
    case VGA_VIRTIO:
        return pci_create_simple(bus, -1, "virtio-vga");
    case VGA_NONE:
    default: /* Other non-PCI types. Checking for unsupported types is already
                done in vl.c. */
        return NULL;
    }
}

/* Whether a given bus number is in range of the secondary
 * bus of the given bridge device. */
static bool pci_secondary_bus_in_range(PCIDevice *dev, int bus_num)
{
    return !(pci_get_word(dev->config + PCI_BRIDGE_CONTROL) &
             PCI_BRIDGE_CTL_BUS_RESET) /* Don't walk the bus if it's reset. */ &&
        dev->config[PCI_SECONDARY_BUS] <= bus_num &&
        bus_num <= dev->config[PCI_SUBORDINATE_BUS];
}

/* Whether a given bus number is in a range of a root bus */
static bool pci_root_bus_in_range(PCIBus *bus, int bus_num)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bus->devices); ++i) {
        PCIDevice *dev = bus->devices[i];

        if (dev && IS_PCI_BRIDGE(dev)) {
            if (pci_secondary_bus_in_range(dev, bus_num)) {
                return true;
            }
        }
    }

    return false;
}

PCIBus *pci_find_bus_nr(PCIBus *bus, int bus_num)
{
    PCIBus *sec;

    if (!bus) {
        return NULL;
    }

    if (pci_bus_num(bus) == bus_num) {
        return bus;
    }

    /* Consider all bus numbers in range for the host pci bridge. */
    if (!pci_bus_is_root(bus) &&
        !pci_secondary_bus_in_range(bus->parent_dev, bus_num)) {
        return NULL;
    }

    /* try child bus */
    for (; bus; bus = sec) {
        QLIST_FOREACH(sec, &bus->child, sibling) {
            if (pci_bus_num(sec) == bus_num) {
                return sec;
            }
            /* PXB buses assumed to be children of bus 0 */
            if (pci_bus_is_root(sec)) {
                if (pci_root_bus_in_range(sec, bus_num)) {
                    break;
                }
            } else {
                if (pci_secondary_bus_in_range(sec->parent_dev, bus_num)) {
                    break;
                }
            }
        }
    }

    return NULL;
}

void pci_for_each_bus_depth_first(PCIBus *bus, pci_bus_ret_fn begin,
                                  pci_bus_fn end, void *parent_state)
{
    PCIBus *sec;
    void *state;

    if (!bus) {
        return;
    }

    if (begin) {
        state = begin(bus, parent_state);
    } else {
        state = parent_state;
    }

    QLIST_FOREACH(sec, &bus->child, sibling) {
        pci_for_each_bus_depth_first(sec, begin, end, state);
    }

    if (end) {
        end(bus, state);
    }
}


PCIDevice *pci_find_device(PCIBus *bus, int bus_num, uint8_t devfn)
{
    bus = pci_find_bus_nr(bus, bus_num);

    if (!bus)
        return NULL;

    return bus->devices[devfn];
}

#define ONBOARD_INDEX_MAX (16 * 1024 - 1)

static void pci_qdev_realize(DeviceState *qdev, Error **errp)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    ObjectClass *klass = OBJECT_CLASS(pc);
    Error *local_err = NULL;
    bool is_default_rom;
    uint16_t class_id;

    /*
     * capped by systemd (see: udev-builtin-net_id.c)
     * as it's the only known user honor it to avoid users
     * misconfigure QEMU and then wonder why acpi-index doesn't work
     */
    if (pci_dev->acpi_index > ONBOARD_INDEX_MAX) {
        error_setg(errp, "acpi-index should be less or equal to %u",
                   ONBOARD_INDEX_MAX);
        return;
    }

    /*
     * make sure that acpi-index is unique across all present PCI devices
     */
    if (pci_dev->acpi_index) {
        GSequence *used_indexes = pci_acpi_index_list();

        if (g_sequence_lookup(used_indexes,
                              GINT_TO_POINTER(pci_dev->acpi_index),
                              g_cmp_uint32, NULL)) {
            error_setg(errp, "a PCI device with acpi-index = %" PRIu32
                       " already exist", pci_dev->acpi_index);
            return;
        }
        g_sequence_insert_sorted(used_indexes,
                                 GINT_TO_POINTER(pci_dev->acpi_index),
                                 g_cmp_uint32, NULL);
    }

    if (pci_dev->romsize != -1 && !is_power_of_2(pci_dev->romsize)) {
        error_setg(errp, "ROM size %u is not a power of two", pci_dev->romsize);
        return;
    }

    /* initialize cap_present for pci_is_express() and pci_config_size(),
     * Note that hybrid PCIs are not set automatically and need to manage
     * QEMU_PCI_CAP_EXPRESS manually */
    if (object_class_dynamic_cast(klass, INTERFACE_PCIE_DEVICE) &&
       !object_class_dynamic_cast(klass, INTERFACE_CONVENTIONAL_PCI_DEVICE)) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    if (object_class_dynamic_cast(klass, INTERFACE_CXL_DEVICE)) {
        pci_dev->cap_present |= QEMU_PCIE_CAP_CXL;
    }

    pci_dev = do_pci_register_device(pci_dev,
                                     object_get_typename(OBJECT(qdev)),
                                     pci_dev->devfn, errp);
    if (pci_dev == NULL)
        return;

    if (pc->realize) {
        pc->realize(pci_dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            do_pci_unregister_device(pci_dev);
            return;
        }
    }

    if (pci_dev->failover_pair_id) {
        if (!pci_bus_is_express(pci_get_bus(pci_dev))) {
            error_setg(errp, "failover primary device must be on "
                             "PCIExpress bus");
            pci_qdev_unrealize(DEVICE(pci_dev));
            return;
        }
        class_id = pci_get_word(pci_dev->config + PCI_CLASS_DEVICE);
        if (class_id != PCI_CLASS_NETWORK_ETHERNET) {
            error_setg(errp, "failover primary device is not an "
                             "Ethernet device");
            pci_qdev_unrealize(DEVICE(pci_dev));
            return;
        }
        if ((pci_dev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION)
            || (PCI_FUNC(pci_dev->devfn) != 0)) {
            error_setg(errp, "failover: primary device must be in its own "
                              "PCI slot");
            pci_qdev_unrealize(DEVICE(pci_dev));
            return;
        }
        qdev->allow_unplug_during_migration = true;
    }

    /* rom loading */
    is_default_rom = false;
    if (pci_dev->romfile == NULL && pc->romfile != NULL) {
        pci_dev->romfile = g_strdup(pc->romfile);
        is_default_rom = true;
    }

    pci_add_option_rom(pci_dev, is_default_rom, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        pci_qdev_unrealize(DEVICE(pci_dev));
        return;
    }

    pci_set_power(pci_dev, true);

    pci_dev->msi_trigger = pci_msi_trigger;
}

PCIDevice *pci_new_multifunction(int devfn, bool multifunction,
                                 const char *name)
{
    DeviceState *dev;

    dev = qdev_new(name);
    qdev_prop_set_int32(dev, "addr", devfn);
    qdev_prop_set_bit(dev, "multifunction", multifunction);
    return PCI_DEVICE(dev);
}

PCIDevice *pci_new(int devfn, const char *name)
{
    return pci_new_multifunction(devfn, false, name);
}

bool pci_realize_and_unref(PCIDevice *dev, PCIBus *bus, Error **errp)
{
    return qdev_realize_and_unref(&dev->qdev, &bus->qbus, errp);
}

PCIDevice *pci_create_simple_multifunction(PCIBus *bus, int devfn,
                                           bool multifunction,
                                           const char *name)
{
    PCIDevice *dev = pci_new_multifunction(devfn, multifunction, name);
    pci_realize_and_unref(dev, bus, &error_fatal);
    return dev;
}

PCIDevice *pci_create_simple(PCIBus *bus, int devfn, const char *name)
{
    return pci_create_simple_multifunction(bus, devfn, false, name);
}

static uint8_t pci_find_space(PCIDevice *pdev, uint8_t size)
{
    int offset = PCI_CONFIG_HEADER_SIZE;
    int i;
    for (i = PCI_CONFIG_HEADER_SIZE; i < PCI_CONFIG_SPACE_SIZE; ++i) {
        if (pdev->used[i])
            offset = i + 1;
        else if (i - offset + 1 == size)
            return offset;
    }
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

static uint8_t pci_find_capability_at_offset(PCIDevice *pdev, uint8_t offset)
{
    uint8_t next, prev, found = 0;

    if (!(pdev->used[offset])) {
        return 0;
    }

    assert(pdev->config[PCI_STATUS] & PCI_STATUS_CAP_LIST);

    for (prev = PCI_CAPABILITY_LIST; (next = pdev->config[prev]);
         prev = next + PCI_CAP_LIST_NEXT) {
        if (next <= offset && next > found) {
            found = next;
        }
    }
    return found;
}

/* Patch the PCI vendor and device ids in a PCI rom image if necessary.
   This is needed for an option rom which is used for more than one device. */
static void pci_patch_ids(PCIDevice *pdev, uint8_t *ptr, uint32_t size)
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
static void pci_add_option_rom(PCIDevice *pdev, bool is_default_rom,
                               Error **errp)
{
    int64_t size;
    g_autofree char *path = NULL;
    void *ptr;
    char name[32];
    const VMStateDescription *vmsd;

    if (!pdev->romfile || !strlen(pdev->romfile)) {
        return;
    }

    if (!pdev->rom_bar) {
        /*
         * Load rom via fw_cfg instead of creating a rom bar,
         * for 0.11 compatibility.
         */
        int class = pci_get_word(pdev->config + PCI_CLASS_DEVICE);

        /*
         * Hot-plugged devices can't use the option ROM
         * if the rom bar is disabled.
         */
        if (DEVICE(pdev)->hotplugged) {
            error_setg(errp, "Hot-plugged device without ROM bar"
                       " can't have an option ROM");
            return;
        }

        if (class == 0x0300) {
            rom_add_vga(pdev->romfile);
        } else {
            rom_add_option(pdev->romfile, -1);
        }
        return;
    }

    path = qemu_find_file(QEMU_FILE_TYPE_BIOS, pdev->romfile);
    if (path == NULL) {
        path = g_strdup(pdev->romfile);
    }

    size = get_image_size(path);
    if (size < 0) {
        error_setg(errp, "failed to find romfile \"%s\"", pdev->romfile);
        return;
    } else if (size == 0) {
        error_setg(errp, "romfile \"%s\" is empty", pdev->romfile);
        return;
    } else if (size > 2 * GiB) {
        error_setg(errp, "romfile \"%s\" too large (size cannot exceed 2 GiB)",
                   pdev->romfile);
        return;
    }
    if (pdev->romsize != -1) {
        if (size > pdev->romsize) {
            error_setg(errp, "romfile \"%s\" (%u bytes) "
                       "is too large for ROM size %u",
                       pdev->romfile, (uint32_t)size, pdev->romsize);
            return;
        }
    } else {
        pdev->romsize = pow2ceil(size);
    }

    vmsd = qdev_get_vmsd(DEVICE(pdev));
    snprintf(name, sizeof(name), "%s.rom",
             vmsd ? vmsd->name : object_get_typename(OBJECT(pdev)));

    pdev->has_rom = true;
    memory_region_init_rom(&pdev->rom, OBJECT(pdev), name, pdev->romsize,
                           &error_fatal);

    ptr = memory_region_get_ram_ptr(&pdev->rom);
    if (load_image_size(path, ptr, size) < 0) {
        error_setg(errp, "failed to load romfile \"%s\"", pdev->romfile);
        return;
    }

    if (is_default_rom) {
        /* Only the default rom images will be patched (if needed). */
        pci_patch_ids(pdev, ptr, size);
    }

    pci_register_bar(pdev, PCI_ROM_SLOT, 0, &pdev->rom);
}

static void pci_del_option_rom(PCIDevice *pdev)
{
    if (!pdev->has_rom)
        return;

    vmstate_unregister_ram(&pdev->rom, &pdev->qdev);
    pdev->has_rom = false;
}

/*
 * On success, pci_add_capability() returns a positive value
 * that the offset of the pci capability.
 * On failure, it sets an error and returns a negative error
 * code.
 */
int pci_add_capability(PCIDevice *pdev, uint8_t cap_id,
                       uint8_t offset, uint8_t size,
                       Error **errp)
{
    uint8_t *config;
    int i, overlapping_cap;

    if (!offset) {
        offset = pci_find_space(pdev, size);
        /* out of PCI config space is programming error */
        assert(offset);
    } else {
        /* Verify that capabilities don't overlap.  Note: device assignment
         * depends on this check to verify that the device is not broken.
         * Should never trigger for emulated devices, but it's helpful
         * for debugging these. */
        for (i = offset; i < offset + size; i++) {
            overlapping_cap = pci_find_capability_at_offset(pdev, i);
            if (overlapping_cap) {
                error_setg(errp, "%s:%02x:%02x.%x "
                           "Attempt to add PCI capability %x at offset "
                           "%x overlaps existing capability %x at offset %x",
                           pci_root_bus_path(pdev), pci_dev_bus_num(pdev),
                           PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn),
                           cap_id, offset, overlapping_cap, i);
                return -EINVAL;
            }
        }
    }

    config = pdev->config + offset;
    config[PCI_CAP_LIST_ID] = cap_id;
    config[PCI_CAP_LIST_NEXT] = pdev->config[PCI_CAPABILITY_LIST];
    pdev->config[PCI_CAPABILITY_LIST] = offset;
    pdev->config[PCI_STATUS] |= PCI_STATUS_CAP_LIST;
    memset(pdev->used + offset, 0xFF, QEMU_ALIGN_UP(size, 4));
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
    /* Make capability writable again */
    memset(pdev->wmask + offset, 0xff, size);
    memset(pdev->w1cmask + offset, 0, size);
    /* Clear cmask as device-specific registers can't be checked */
    memset(pdev->cmask + offset, 0, size);
    memset(pdev->used + offset, 0, QEMU_ALIGN_UP(size, 4));

    if (!pdev->config[PCI_CAPABILITY_LIST])
        pdev->config[PCI_STATUS] &= ~PCI_STATUS_CAP_LIST;
}

uint8_t pci_find_capability(PCIDevice *pdev, uint8_t cap_id)
{
    return pci_find_capability_list(pdev, cap_id, NULL);
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
    char name[33];
    int has_func = !!PCI_FUNC(d->devfn);

    return g_strdup_printf("%s@%x%s%.*x",
                           pci_dev_fw_name(dev, name, sizeof(name)),
                           PCI_SLOT(d->devfn),
                           has_func ? "," : "",
                           has_func,
                           PCI_FUNC(d->devfn));
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
    const char *root_bus_path;
    int root_bus_len;
    char slot[] = ":SS.F";
    int slot_len = sizeof slot - 1 /* For '\0' */;
    int path_len;
    char *path, *p;
    int s;

    root_bus_path = pci_root_bus_path(d);
    root_bus_len = strlen(root_bus_path);

    /* Calculate # of slots on path between device and root. */;
    slot_depth = 0;
    for (t = d; t; t = pci_get_bus(t)->parent_dev) {
        ++slot_depth;
    }

    path_len = root_bus_len + slot_len * slot_depth;

    /* Allocate memory, fill in the terminating null byte. */
    path = g_malloc(path_len + 1 /* For '\0' */);
    path[path_len] = '\0';

    memcpy(path, root_bus_path, root_bus_len);

    /* Fill in slot numbers. We walk up from device to root, so need to print
     * them in the reverse order, last to first. */
    p = path + path_len;
    for (t = d; t; t = pci_get_bus(t)->parent_dev) {
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
    if (object_dynamic_cast(OBJECT(qdev), TYPE_PCI_DEVICE)) {
        *pdev = PCI_DEVICE(qdev);
        return 0;
    }
    return -EINVAL;
}

int pci_qdev_find_device(const char *id, PCIDevice **pdev)
{
    PCIHostState *host_bridge;
    int rc = -ENODEV;

    QLIST_FOREACH(host_bridge, &pci_host_bridges, next) {
        int tmp = pci_qdev_find_recursive(host_bridge->bus, id, pdev);
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

MemoryRegion *pci_address_space(PCIDevice *dev)
{
    return pci_get_bus(dev)->address_space_mem;
}

MemoryRegion *pci_address_space_io(PCIDevice *dev)
{
    return pci_get_bus(dev)->address_space_io;
}

static void pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = pci_qdev_realize;
    k->unrealize = pci_qdev_unrealize;
    k->bus_type = TYPE_PCI_BUS;
    device_class_set_props(k, pci_props);
}

static void pci_device_class_base_init(ObjectClass *klass, void *data)
{
    if (!object_class_is_abstract(klass)) {
        ObjectClass *conventional =
            object_class_dynamic_cast(klass, INTERFACE_CONVENTIONAL_PCI_DEVICE);
        ObjectClass *pcie =
            object_class_dynamic_cast(klass, INTERFACE_PCIE_DEVICE);
        ObjectClass *cxl =
            object_class_dynamic_cast(klass, INTERFACE_CXL_DEVICE);
        assert(conventional || pcie || cxl);
    }
}

AddressSpace *pci_device_iommu_address_space(PCIDevice *dev)
{
    PCIBus *bus = pci_get_bus(dev);
    PCIBus *iommu_bus = bus;
    uint8_t devfn = dev->devfn;

    while (iommu_bus && !iommu_bus->iommu_fn && iommu_bus->parent_dev) {
        PCIBus *parent_bus = pci_get_bus(iommu_bus->parent_dev);

        /*
         * The requester ID of the provided device may be aliased, as seen from
         * the IOMMU, due to topology limitations.  The IOMMU relies on a
         * requester ID to provide a unique AddressSpace for devices, but
         * conventional PCI buses pre-date such concepts.  Instead, the PCIe-
         * to-PCI bridge creates and accepts transactions on behalf of down-
         * stream devices.  When doing so, all downstream devices are masked
         * (aliased) behind a single requester ID.  The requester ID used
         * depends on the format of the bridge devices.  Proper PCIe-to-PCI
         * bridges, with a PCIe capability indicating such, follow the
         * guidelines of chapter 2.3 of the PCIe-to-PCI/X bridge specification,
         * where the bridge uses the seconary bus as the bridge portion of the
         * requester ID and devfn of 00.0.  For other bridges, typically those
         * found on the root complex such as the dmi-to-pci-bridge, we follow
         * the convention of typical bare-metal hardware, which uses the
         * requester ID of the bridge itself.  There are device specific
         * exceptions to these rules, but these are the defaults that the
         * Linux kernel uses when determining DMA aliases itself and believed
         * to be true for the bare metal equivalents of the devices emulated
         * in QEMU.
         */
        if (!pci_bus_is_express(iommu_bus)) {
            PCIDevice *parent = iommu_bus->parent_dev;

            if (pci_is_express(parent) &&
                pcie_cap_get_type(parent) == PCI_EXP_TYPE_PCI_BRIDGE) {
                devfn = PCI_DEVFN(0, 0);
                bus = iommu_bus;
            } else {
                devfn = parent->devfn;
                bus = parent_bus;
            }
        }

        iommu_bus = parent_bus;
    }
    if (!pci_bus_bypass_iommu(bus) && iommu_bus && iommu_bus->iommu_fn) {
        return iommu_bus->iommu_fn(bus, iommu_bus->iommu_opaque, devfn);
    }
    return &address_space_memory;
}

void pci_setup_iommu(PCIBus *bus, PCIIOMMUFunc fn, void *opaque)
{
    bus->iommu_fn = fn;
    bus->iommu_opaque = opaque;
}

static void pci_dev_get_w64(PCIBus *b, PCIDevice *dev, void *opaque)
{
    Range *range = opaque;
    uint16_t cmd = pci_get_word(dev->config + PCI_COMMAND);
    int i;

    if (!(cmd & PCI_COMMAND_MEMORY)) {
        return;
    }

    if (IS_PCI_BRIDGE(dev)) {
        pcibus_t base = pci_bridge_get_base(dev, PCI_BASE_ADDRESS_MEM_PREFETCH);
        pcibus_t limit = pci_bridge_get_limit(dev, PCI_BASE_ADDRESS_MEM_PREFETCH);

        base = MAX(base, 0x1ULL << 32);

        if (limit >= base) {
            Range pref_range;
            range_set_bounds(&pref_range, base, limit);
            range_extend(range, &pref_range);
        }
    }
    for (i = 0; i < PCI_NUM_REGIONS; ++i) {
        PCIIORegion *r = &dev->io_regions[i];
        pcibus_t lob, upb;
        Range region_range;

        if (!r->size ||
            (r->type & PCI_BASE_ADDRESS_SPACE_IO) ||
            !(r->type & PCI_BASE_ADDRESS_MEM_TYPE_64)) {
            continue;
        }

        lob = pci_bar_address(dev, i, r->type, r->size);
        upb = lob + r->size - 1;
        if (lob == PCI_BAR_UNMAPPED) {
            continue;
        }

        lob = MAX(lob, 0x1ULL << 32);

        if (upb >= lob) {
            range_set_bounds(&region_range, lob, upb);
            range_extend(range, &region_range);
        }
    }
}

void pci_bus_get_w64_range(PCIBus *bus, Range *range)
{
    range_make_empty(range);
    pci_for_each_device_under_bus(bus, pci_dev_get_w64, range);
}

static bool pcie_has_upstream_port(PCIDevice *dev)
{
    PCIDevice *parent_dev = pci_bridge_get_device(pci_get_bus(dev));

    /* Device associated with an upstream port.
     * As there are several types of these, it's easier to check the
     * parent device: upstream ports are always connected to
     * root or downstream ports.
     */
    return parent_dev &&
        pci_is_express(parent_dev) &&
        parent_dev->exp.exp_cap &&
        (pcie_cap_get_type(parent_dev) == PCI_EXP_TYPE_ROOT_PORT ||
         pcie_cap_get_type(parent_dev) == PCI_EXP_TYPE_DOWNSTREAM);
}

PCIDevice *pci_get_function_0(PCIDevice *pci_dev)
{
    PCIBus *bus = pci_get_bus(pci_dev);

    if(pcie_has_upstream_port(pci_dev)) {
        /* With an upstream PCIe port, we only support 1 device at slot 0 */
        return bus->devices[0];
    } else {
        /* Other bus types might support multiple devices at slots 0-31 */
        return bus->devices[PCI_DEVFN(PCI_SLOT(pci_dev->devfn), 0)];
    }
}

MSIMessage pci_get_msi_message(PCIDevice *dev, int vector)
{
    MSIMessage msg;
    if (msix_enabled(dev)) {
        msg = msix_get_message(dev, vector);
    } else if (msi_enabled(dev)) {
        msg = msi_get_message(dev, vector);
    } else {
        /* Should never happen */
        error_report("%s: unknown interrupt type", __func__);
        abort();
    }
    return msg;
}

void pci_set_power(PCIDevice *d, bool state)
{
    if (d->has_power == state) {
        return;
    }

    d->has_power = state;
    pci_update_mappings(d);
    memory_region_set_enabled(&d->bus_master_enable_region,
                              (pci_get_word(d->config + PCI_COMMAND)
                               & PCI_COMMAND_MASTER) && d->has_power);
    if (!d->has_power) {
        pci_device_reset(d);
    }
}

static const TypeInfo pci_device_type_info = {
    .name = TYPE_PCI_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .abstract = true,
    .class_size = sizeof(PCIDeviceClass),
    .class_init = pci_device_class_init,
    .class_base_init = pci_device_class_base_init,
};

static void pci_register_types(void)
{
    type_register_static(&pci_bus_info);
    type_register_static(&pcie_bus_info);
    type_register_static(&cxl_bus_info);
    type_register_static(&conventional_pci_interface_info);
    type_register_static(&cxl_interface_info);
    type_register_static(&pcie_interface_info);
    type_register_static(&pci_device_type_info);
}

type_init(pci_register_types)
