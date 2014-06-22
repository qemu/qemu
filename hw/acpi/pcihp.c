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
 * License version 2 as published by the Free Software Foundation.
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

#include "hw/acpi/pcihp.h"

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/acpi/acpi.h"
#include "sysemu/sysemu.h"
#include "qemu/range.h"
#include "exec/ioport.h"
#include "exec/address-spaces.h"
#include "hw/pci/pci_bus.h"
#include "qom/qom-qobject.h"
#include "qapi/qmp/qint.h"

//#define DEBUG

#ifdef DEBUG
# define ACPI_PCIHP_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define ACPI_PCIHP_DPRINTF(format, ...)     do { } while (0)
#endif

#define ACPI_PCI_HOTPLUG_STATUS 2
#define ACPI_PCIHP_ADDR 0xae00
#define ACPI_PCIHP_SIZE 0x0014
#define ACPI_PCIHP_LEGACY_SIZE 0x000f
#define PCI_UP_BASE 0x0000
#define PCI_DOWN_BASE 0x0004
#define PCI_EJ_BASE 0x0008
#define PCI_RMV_BASE 0x000c
#define PCI_SEL_BASE 0x0010

typedef struct AcpiPciHpFind {
    int bsel;
    PCIBus *bus;
} AcpiPciHpFind;

static int acpi_pcihp_get_bsel(PCIBus *bus)
{
    Error *local_err = NULL;
    int64_t bsel = object_property_get_int(OBJECT(bus), ACPI_PCIHP_PROP_BSEL,
                                           &local_err);

    if (local_err || bsel < 0 || bsel >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
        if (local_err) {
            error_free(local_err);
        }
        return -1;
    } else {
        return bsel;
    }
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
    BusChild *kid, *next;
    int slot = ffs(slots) - 1;
    PCIBus *bus = acpi_pcihp_find_hotplug_bus(s, bsel);

    if (!bus) {
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

void acpi_pcihp_reset(AcpiPciHpState *s)
{
    acpi_pcihp_update(s);
}

void acpi_pcihp_device_plug_cb(ACPIREGS *ar, qemu_irq irq, AcpiPciHpState *s,
                               DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    int slot = PCI_SLOT(pdev->devfn);
    int bsel = acpi_pcihp_get_bsel(pdev->bus);
    if (bsel < 0) {
        error_setg(errp, "Unsupported bus. Bus doesn't have property '"
                   ACPI_PCIHP_PROP_BSEL "' set");
        return;
    }

    /* Don't send event when device is enabled during qemu machine creation:
     * it is present on boot, no hotplug event is necessary. We do send an
     * event when the device is disabled later. */
    if (!dev->hotplugged) {
        return;
    }

    s->acpi_pcihp_pci_status[bsel].up |= (1U << slot);

    ar->gpe.sts[0] |= ACPI_PCI_HOTPLUG_STATUS;
    acpi_update_sci(ar, irq);
}

void acpi_pcihp_device_unplug_cb(ACPIREGS *ar, qemu_irq irq, AcpiPciHpState *s,
                                 DeviceState *dev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    int slot = PCI_SLOT(pdev->devfn);
    int bsel = acpi_pcihp_get_bsel(pdev->bus);
    if (bsel < 0) {
        error_setg(errp, "Unsupported bus. Bus doesn't have property '"
                   ACPI_PCIHP_PROP_BSEL "' set");
        return;
    }

    s->acpi_pcihp_pci_status[bsel].down |= (1U << slot);

    ar->gpe.sts[0] |= ACPI_PCI_HOTPLUG_STATUS;
    acpi_update_sci(ar, irq);
}

static uint64_t pci_read(void *opaque, hwaddr addr, unsigned int size)
{
    AcpiPciHpState *s = opaque;
    uint32_t val = 0;
    int bsel = s->hotplug_select;

    if (bsel < 0 || bsel > ACPI_PCIHP_MAX_HOTPLUG_BUS) {
        return 0;
    }

    switch (addr) {
    case PCI_UP_BASE:
        val = s->acpi_pcihp_pci_status[bsel].up;
        if (!s->legacy_piix) {
            s->acpi_pcihp_pci_status[bsel].up = 0;
        }
        ACPI_PCIHP_DPRINTF("pci_up_read %" PRIu32 "\n", val);
        break;
    case PCI_DOWN_BASE:
        val = s->acpi_pcihp_pci_status[bsel].down;
        ACPI_PCIHP_DPRINTF("pci_down_read %" PRIu32 "\n", val);
        break;
    case PCI_EJ_BASE:
        /* No feature defined yet */
        ACPI_PCIHP_DPRINTF("pci_features_read %" PRIu32 "\n", val);
        break;
    case PCI_RMV_BASE:
        val = s->acpi_pcihp_pci_status[bsel].hotplug_enable;
        ACPI_PCIHP_DPRINTF("pci_rmv_read %" PRIu32 "\n", val);
        break;
    case PCI_SEL_BASE:
        val = s->hotplug_select;
        ACPI_PCIHP_DPRINTF("pci_sel_read %" PRIu32 "\n", val);
    default:
        break;
    }

    return val;
}

static void pci_write(void *opaque, hwaddr addr, uint64_t data,
                      unsigned int size)
{
    AcpiPciHpState *s = opaque;
    switch (addr) {
    case PCI_EJ_BASE:
        if (s->hotplug_select >= ACPI_PCIHP_MAX_HOTPLUG_BUS) {
            break;
        }
        acpi_pcihp_eject_slot(s, s->hotplug_select, data);
        ACPI_PCIHP_DPRINTF("pciej write %" HWADDR_PRIx " <== %" PRIu64 "\n",
                      addr, data);
        break;
    case PCI_SEL_BASE:
        s->hotplug_select = data;
        ACPI_PCIHP_DPRINTF("pcisel write %" HWADDR_PRIx " <== %" PRIu64 "\n",
                      addr, data);
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

void acpi_pcihp_init(AcpiPciHpState *s, PCIBus *root_bus,
                     MemoryRegion *address_space_io, bool bridges_enabled)
{
    uint16_t io_size = ACPI_PCIHP_SIZE;

    s->root= root_bus;
    s->legacy_piix = !bridges_enabled;

    if (s->legacy_piix) {
        unsigned *bus_bsel = g_malloc(sizeof *bus_bsel);

        io_size = ACPI_PCIHP_LEGACY_SIZE;

        *bus_bsel = ACPI_PCIHP_BSEL_DEFAULT;
        object_property_add_uint32_ptr(OBJECT(root_bus), ACPI_PCIHP_PROP_BSEL,
                                       bus_bsel, NULL);
    }

    memory_region_init_io(&s->io, NULL, &acpi_pcihp_io_ops, s,
                          "acpi-pci-hotplug", io_size);
    memory_region_add_subregion(address_space_io, ACPI_PCIHP_ADDR, &s->io);
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
