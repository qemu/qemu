/*
 * pcie_port.h
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_PCIE_PORT_H
#define QEMU_PCIE_PORT_H

#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_device.h"
#include "qom/object.h"

#define TYPE_PCIE_PORT "pcie-port"
OBJECT_DECLARE_SIMPLE_TYPE(PCIEPort, PCIE_PORT)

struct PCIEPort {
    /*< private >*/
    PCIBridge   parent_obj;
    /*< public >*/

    /* pci express switch port */
    uint8_t     port;
};

void pcie_port_init_reg(PCIDevice *d);

PCIDevice *pcie_find_port_by_pn(PCIBus *bus, uint8_t pn);
PCIDevice *pcie_find_port_first(PCIBus *bus);
int pcie_count_ds_ports(PCIBus *bus);

#define TYPE_PCIE_SLOT "pcie-slot"
OBJECT_DECLARE_SIMPLE_TYPE(PCIESlot, PCIE_SLOT)

struct PCIESlot {
    /*< private >*/
    PCIEPort    parent_obj;
    /*< public >*/

    /* pci express switch port with slot */
    uint8_t     chassis;
    uint16_t    slot;

    PCIExpLinkSpeed speed;
    PCIExpLinkWidth width;

    /* Disable ACS (really for a pcie_root_port) */
    bool        disable_acs;

    /* Indicates whether any type of hot-plug is allowed on the slot */
    bool        hotplug;

    /* broken ACPI hotplug compat knob to preserve 6.1 ABI intact */
    bool        hide_native_hotplug_cap;

    QLIST_ENTRY(PCIESlot) next;
};

void pcie_chassis_create(uint8_t chassis_number);
PCIESlot *pcie_chassis_find_slot(uint8_t chassis, uint16_t slot);
int pcie_chassis_add_slot(struct PCIESlot *slot);
void pcie_chassis_del_slot(PCIESlot *s);

#define TYPE_PCIE_ROOT_PORT         "pcie-root-port-base"
typedef struct PCIERootPortClass PCIERootPortClass;
DECLARE_CLASS_CHECKERS(PCIERootPortClass, PCIE_ROOT_PORT,
                       TYPE_PCIE_ROOT_PORT)

struct PCIERootPortClass {
    PCIDeviceClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;

    uint8_t (*aer_vector)(const PCIDevice *dev);
    int (*interrupts_init)(PCIDevice *dev, Error **errp);
    void (*interrupts_uninit)(PCIDevice *dev);

    int exp_offset;
    int aer_offset;
    int ssvid_offset;
    int acs_offset;    /* If nonzero, optional ACS capability offset */
    int ssid;
};

#endif /* QEMU_PCIE_PORT_H */
