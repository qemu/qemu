/*
 * QEMU<->ACPI BIOS PCI hotplug interface
 *
 * QEMU supports PCI hotplug via ACPI. This module
 * implements the interface between QEMU and the ACPI BIOS.
 * Interface specification - see docs/specs/acpi_pci_hotplug.txt
 *
 * Copyright (c) 2013, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/acpi/pcihp.h"

#include "hw/pci-host/i440fx.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/i386/acpi-build.h"
#include "hw/acpi/acpi.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qom/qom-qobject.h"
#include "trace.h"

#define ACPI_PCIHP_SIZE 0x0018
#define PCI_UP_BASE 0x0000
#define PCI_DOWN_BASE 0x0004
#define PCI_EJ_BASE 0x0008
#define PCI_RMV_BASE 0x000c
#define PCI_SEL_BASE 0x0010
#define PCI_AIDX_BASE 0x0014

typedef struct AcpiPciHpFind {
    int bsel;
    PCIBus *bus;
} AcpiPciHpFind;

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

static int acpi_pcihp_get_bsel(PCIBus *bus)
{
    Error *local_err = NULL;
    uint64_t bsel = object_property_get_uint(OBJECT(bus), ACPI_PCIHP_PROP_BSEL,
                                             &local_err);

    if (local_err || bsel >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
        if (local_err) {
            error_free(local_err);
        }
        return -1;
    } else {
        return bsel;
    }
}

/* Assign BSEL property to all buses.  In the future, this can be changed
 * to only assign to buses that support hotplug.
 */
static void *acpi_set_bsel(PCIBus *bus, void *opaque)
{
    unsigned *bsel_alloc = opaque;
    unsigned *bus_bsel;

    if (qbus_is_hotpluggable(BUS(bus))) {
        bus_bsel = g_malloc(sizeof *bus_bsel);

        *bus_bsel = (*bsel_alloc)++;
        object_property_add_uint32_ptr(OBJECT(bus), ACPI_PCIHP_PROP_BSEL,
                                       bus_bsel, OBJ_PROP_FLAG_READ);
    }

    return bsel_alloc;
}

static void acpi_set_pci_info(void)
{
    static bool bsel_is_set;
    Object *host = acpi_get_i386_pci_host();
    PCIBus *bus;
    unsigned bsel_alloc = ACPI_PCIHP_BSEL_DEFAULT;

    if (bsel_is_set) {
        return;
    }
    bsel_is_set = true;

    if (!host) {
        return;
    }

    bus = PCI_HOST_BRIDGE(host)->bus;
    if (bus) {
        /* Scan all PCI buses. Set property to enable acpi based hotplug. */
        pci_for_each_bus_depth_first(bus, acpi_set_bsel, NULL, &bsel_alloc);
    }
}

static void acpi_pcihp_disable_root_bus(void)
{
    static bool root_hp_disabled;
    Object *host = acpi_get_i386_pci_host();
    PCIBus *bus;

    if (root_hp_disabled) {
        return;
    }

    bus = PCI_HOST_BRIDGE(host)->bus;
    if (bus) {
        /* setting the hotplug handler to NULL makes the bus non-hotpluggable */
        qbus_set_hotplug_handler(BUS(bus), NULL);
    }
    root_hp_disabled = true;
    return;
}

static void acpi_pcihp_test_hotplug_bus(PCIBus *bus, void *opaque)
{
    AcpiPciHpFind *find = opaque;
    if (find->bsel == acpi_pcihp_get_bsel(bus)) {
        find->bus = bus;
    }
}

static PCIBus *acpi_pcihp_find_hotplug_bus(AcpiPciHpState *s, int bsel)
{
    AcpiPciHpFind find = { .bsel = bsel, .bus = NULL };

    if (bsel < 0) {
        return NULL;
    }

    pci_for_each_bus(s->root, acpi_pcihp_test_hotplug_bus, &find);

    /* Make bsel 0 eject root bus if bsel property is not set,
     * for compatibility with non acpi setups.
     * TODO: really needed?
     */
    if (!bsel && !find.bus) {
        find.bus = s->root;
    }

    /*
     * Check if find.bus is actually hotpluggable. If bsel is set to
     * NULL for example on the root bus in order to make it
     * non-hotpluggable, find.bus will match the root bus when bsel
     * is 0. See acpi_pcihp_test_hotplug_bus() above. Since the
     * bus is not hotpluggable however, we should not select the bus.
     * Instead, we should set find.bus to NULL in that case. In the check
     * below, we generalize this case for all buses, not just the root bus.
     * The callers of this function check for a null return value and
     * handle them appropriately.
     */
    if (find.bus && !qbus_is_hotpluggable(BUS(find.bus))) {
        find.bus = NULL;
    }
    return find.bus;
}

static bool acpi_pcihp_pc_no_hotplug(AcpiPciHpState *s, PCIDevice *dev)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(dev);
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    /*
     * ACPI doesn't allow hotplug of bridge devices.  Don't allow
     * hot-unplug of bridge devices unless they were added by hotplug
     * (and so, not described by acpi).
     */
    return (pc->is_bridge && !dev->qdev.hotplugged) || !dc->hotpluggable;
}

static void acpi_pcihp_eject_slot(AcpiPciHpState *s, unsigned bsel, unsigned slots)
{
    HotplugHandler *hotplug_ctrl;
    BusChild *kid, *next;
    int slot = ctz32(slots);
    PCIBus *bus = acpi_pcihp_find_hotplug_bus(s, bsel);

    trace_acpi_pci_eject_slot(bsel, slot);

    if (!bus || slot > 31) {
        return;
    }

    /* Mark request as complete */
    s->acpi_pcihp_pci_status[bsel].down &= ~(1U << slot);
    s->acpi_pcihp_pci_status[bsel].up &= ~(1U << slot);

    QTAILQ_FOREACH_SAFE(kid, &bus->qbus.children, sibling, next) {
        DeviceState *qdev = kid->child;
        PCIDevice *dev = PCI_DEVICE(qdev);
        if (PCI_SLOT(dev->devfn) == slot) {
            if (!acpi_pcihp_pc_no_hotplug(s, dev)) {
                hotplug_ctrl = qdev_get_hotplug_handler(qdev);
                hotplug_handler_unplug(hotplug_ctrl, qdev, &error_abort);
                object_unparent(OBJECT(qdev));
            }
        }
    }
}

static void acpi_pcihp_update_hotplug_bus(AcpiPciHpState *s, int bsel)
{
    BusChild *kid, *next;
    PCIBus *bus = acpi_pcihp_find_hotplug_bus(s, bsel);

    /* Execute any pending removes during reset */
    while (s->acpi_pcihp_pci_status[bsel].down) {
        acpi_pcihp_eject_slot(s, bsel, s->acpi_pcihp_pci_status[bsel].down);
    }

    s->acpi_pcihp_pci_status[bsel].hotplug_enable = ~0;

    if (!bus) {
        return;
    }
    QTAILQ_FOREACH_SAFE(kid, &bus->qbus.children, sibling, next) {
        DeviceState *qdev = kid->child;
        PCIDevice *pdev = PCI_DEVICE(qdev);
        int slot = PCI_SLOT(pdev->devfn);

        if (acpi_pcihp_pc_no_hotplug(s, pdev)) {
            s->acpi_pcihp_pci_status[bsel].hotplug_enable &= ~(1U << slot);
        }
    }
}

static void acpi_pcihp_update(AcpiPciHpState *s)
{
    int i;

    for (i = 0; i < ACPI_PCIHP_MAX_HOTPLUG_BUS; ++i) {
        acpi_pcihp_update_hotplug_bus(s, i);
    }
}

void acpi_pcihp_reset(AcpiPciHpState *s, bool acpihp_root_off)
{
    if (acpihp_root_off) {
        acpi_pcihp_disable_root_bus();
    }
    acpi_set_pci_info();
    acpi_pcihp_update(s);
}

#define ONBOARD_INDEX_MAX (16 * 1024 - 1)

void acpi_pcihp_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* Only hotplugged devices need the hotplug capability. */
    if (dev->hotplugged &&
        acpi_pcihp_get_bsel(pci_get_bus(pdev)) < 0) {
        error_setg(errp, "Unsupported bus. Bus doesn't have property '"
                   ACPI_PCIHP_PROP_BSEL "' set");
        return;
    }

    /*
     * capped by systemd (see: udev-builtin-net_id.c)
     * as it's the only known user honor it to avoid users
     * misconfigure QEMU and then wonder why acpi-index doesn't work
     */
    if (pdev->acpi_index > ONBOARD_INDEX_MAX) {
        error_setg(errp, "acpi-index should be less or equal to %u",
                   ONBOARD_INDEX_MAX);
        return;
    }

    /*
     * make sure that acpi-index is unique across all present PCI devices
     */
    if (pdev->acpi_index) {
        GSequence *used_indexes = pci_acpi_index_list();

        if (g_sequence_lookup(used_indexes, GINT_TO_POINTER(pdev->acpi_index),
                              g_cmp_uint32, NULL)) {
            error_setg(errp, "a PCI device with acpi-index = %" PRIu32
                       " already exist", pdev->acpi_index);
            return;
        }
        g_sequence_insert_sorted(used_indexes,
                                 GINT_TO_POINTER(pdev->acpi_index),
                                 g_cmp_uint32, NULL);
    }
}

void acpi_pcihp_device_plug_cb(HotplugHandler *hotplug_dev, AcpiPciHpState *s,
                               DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    int slot = PCI_SLOT(pdev->devfn);
    int bsel;

    /* Don't send event when device is enabled during qemu machine creation:
     * it is present on boot, no hotplug event is necessary. We do send an
     * event when the device is disabled later. */
    if (!dev->hotplugged) {
        /*
         * Overwrite the default hotplug handler with the ACPI PCI one
         * for cold plugged bridges only.
         */
        if (!s->legacy_piix &&
            object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
            PCIBus *sec = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));

            /* Remove all hot-plug handlers if hot-plug is disabled on slot */
            if (object_dynamic_cast(OBJECT(dev), TYPE_PCIE_SLOT) &&
                !PCIE_SLOT(pdev)->hotplug) {
                qbus_set_hotplug_handler(BUS(sec), NULL);
                return;
            }

            qbus_set_hotplug_handler(BUS(sec), OBJECT(hotplug_dev));
            /* We don't have to overwrite any other hotplug handler yet */
            assert(QLIST_EMPTY(&sec->child));
        }

        return;
    }

    bsel = acpi_pcihp_get_bsel(pci_get_bus(pdev));
    g_assert(bsel >= 0);
    s->acpi_pcihp_pci_status[bsel].up |= (1U << slot);
    acpi_send_event(DEVICE(hotplug_dev), ACPI_PCI_HOTPLUG_STATUS);
}

void acpi_pcihp_device_unplug_cb(HotplugHandler *hotplug_dev, AcpiPciHpState *s,
                                 DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);

    trace_acpi_pci_unplug(PCI_SLOT(pdev->devfn),
                          acpi_pcihp_get_bsel(pci_get_bus(pdev)));

    /*
     * clean up acpi-index so it could reused by another device
     */
    if (pdev->acpi_index) {
        GSequence *used_indexes = pci_acpi_index_list();

        g_sequence_remove(g_sequence_lookup(used_indexes,
                          GINT_TO_POINTER(pdev->acpi_index),
                          g_cmp_uint32, NULL));
    }

    qdev_unrealize(dev);
}

void acpi_pcihp_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                         AcpiPciHpState *s, DeviceState *dev,
                                         Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    int slot = PCI_SLOT(pdev->devfn);
    int bsel = acpi_pcihp_get_bsel(pci_get_bus(pdev));

    trace_acpi_pci_unplug_request(bsel, slot);

    if (bsel < 0) {
        error_setg(errp, "Unsupported bus. Bus doesn't have property '"
                   ACPI_PCIHP_PROP_BSEL "' set");
        return;
    }

    s->acpi_pcihp_pci_status[bsel].down |= (1U << slot);
    acpi_send_event(DEVICE(hotplug_dev), ACPI_PCI_HOTPLUG_STATUS);
}

static uint64_t pci_read(void *opaque, hwaddr addr, unsigned int size)
{
    AcpiPciHpState *s = opaque;
    uint32_t val = 0;
    int bsel = s->hotplug_select;

    if (bsel < 0 || bsel >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
        return 0;
    }

    switch (addr) {
    case PCI_UP_BASE:
        val = s->acpi_pcihp_pci_status[bsel].up;
        if (!s->legacy_piix) {
            s->acpi_pcihp_pci_status[bsel].up = 0;
        }
        trace_acpi_pci_up_read(val);
        break;
    case PCI_DOWN_BASE:
        val = s->acpi_pcihp_pci_status[bsel].down;
        trace_acpi_pci_down_read(val);
        break;
    case PCI_EJ_BASE:
        trace_acpi_pci_features_read(val);
        break;
    case PCI_RMV_BASE:
        val = s->acpi_pcihp_pci_status[bsel].hotplug_enable;
        trace_acpi_pci_rmv_read(val);
        break;
    case PCI_SEL_BASE:
        val = s->hotplug_select;
        trace_acpi_pci_sel_read(val);
        break;
    case PCI_AIDX_BASE:
        val = s->acpi_index;
        s->acpi_index = 0;
        trace_acpi_pci_acpi_index_read(val);
        break;
    default:
        break;
    }

    return val;
}

static void pci_write(void *opaque, hwaddr addr, uint64_t data,
                      unsigned int size)
{
    int slot;
    PCIBus *bus;
    BusChild *kid, *next;
    AcpiPciHpState *s = opaque;

    s->acpi_index = 0;
    switch (addr) {
    case PCI_AIDX_BASE:
        /*
         * fetch acpi-index for specified slot so that follow up read from
         * PCI_AIDX_BASE can return it to guest
         */
        slot = ctz32(data);

        if (s->hotplug_select >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
            break;
        }

        bus = acpi_pcihp_find_hotplug_bus(s, s->hotplug_select);
        QTAILQ_FOREACH_SAFE(kid, &bus->qbus.children, sibling, next) {
            Object *o = OBJECT(kid->child);
            PCIDevice *dev = PCI_DEVICE(o);
            if (PCI_SLOT(dev->devfn) == slot) {
                s->acpi_index = object_property_get_uint(o, "acpi-index", NULL);
                break;
            }
        }
        trace_acpi_pci_acpi_index_write(s->hotplug_select, slot, s->acpi_index);
        break;
    case PCI_EJ_BASE:
        if (s->hotplug_select >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
            break;
        }
        acpi_pcihp_eject_slot(s, s->hotplug_select, data);
        trace_acpi_pci_ej_write(addr, data);
        break;
    case PCI_SEL_BASE:
        s->hotplug_select = s->legacy_piix ? ACPI_PCIHP_BSEL_DEFAULT : data;
        trace_acpi_pci_sel_write(addr, data);
    default:
        break;
    }
}

static const MemoryRegionOps acpi_pcihp_io_ops = {
    .read = pci_read,
    .write = pci_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void acpi_pcihp_init(Object *owner, AcpiPciHpState *s, PCIBus *root_bus,
                     MemoryRegion *address_space_io, bool bridges_enabled,
                     uint16_t io_base)
{
    s->io_len = ACPI_PCIHP_SIZE;
    s->io_base = io_base;

    s->root = root_bus;
    s->legacy_piix = !bridges_enabled;

    memory_region_init_io(&s->io, owner, &acpi_pcihp_io_ops, s,
                          "acpi-pci-hotplug", s->io_len);
    memory_region_add_subregion(address_space_io, s->io_base, &s->io);

    object_property_add_uint16_ptr(owner, ACPI_PCIHP_IO_BASE_PROP, &s->io_base,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint16_ptr(owner, ACPI_PCIHP_IO_LEN_PROP, &s->io_len,
                                   OBJ_PROP_FLAG_READ);
}

bool vmstate_acpi_pcihp_use_acpi_index(void *opaque, int version_id)
{
     AcpiPciHpState *s = opaque;
     return s->acpi_index;
}

const VMStateDescription vmstate_acpi_pcihp_pci_status = {
    .name = "acpi_pcihp_pci_status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(up, AcpiPciHpPciStatus),
        VMSTATE_UINT32(down, AcpiPciHpPciStatus),
        VMSTATE_END_OF_LIST()
    }
};
