/*
 * pcie_port.c
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

#include "hw/pci/pcie_port.h"

void pcie_port_init_reg(PCIDevice *d)
{
    /* Unlike pci bridge,
       66MHz and fast back to back don't apply to pci express port. */
    pci_set_word(d->config + PCI_STATUS, 0);
    pci_set_word(d->config + PCI_SEC_STATUS, 0);

    /*
     * Unlike conventional pci bridge, for some bits the spec states:
     * Does not apply to PCI Express and must be hardwired to 0.
     */
    pci_word_test_and_clear_mask(d->wmask + PCI_BRIDGE_CONTROL,
                                 PCI_BRIDGE_CTL_MASTER_ABORT |
                                 PCI_BRIDGE_CTL_FAST_BACK |
                                 PCI_BRIDGE_CTL_DISCARD |
                                 PCI_BRIDGE_CTL_SEC_DISCARD |
                                 PCI_BRIDGE_CTL_DISCARD_STATUS |
                                 PCI_BRIDGE_CTL_DISCARD_SERR);
}

/**************************************************************************
 * (chassis number, pcie physical slot number) -> pcie slot conversion
 */
struct PCIEChassis {
    uint8_t     number;

    QLIST_HEAD(, PCIESlot) slots;
    QLIST_ENTRY(PCIEChassis) next;
};

static QLIST_HEAD(, PCIEChassis) chassis = QLIST_HEAD_INITIALIZER(chassis);

static struct PCIEChassis *pcie_chassis_find(uint8_t chassis_number)
{
    struct PCIEChassis *c;
    QLIST_FOREACH(c, &chassis, next) {
        if (c->number == chassis_number) {
            break;
        }
    }
    return c;
}

void pcie_chassis_create(uint8_t chassis_number)
{
    struct PCIEChassis *c;
    c = pcie_chassis_find(chassis_number);
    if (c) {
        return;
    }
    c = g_malloc0(sizeof(*c));
    c->number = chassis_number;
    QLIST_INIT(&c->slots);
    QLIST_INSERT_HEAD(&chassis, c, next);
}

static PCIESlot *pcie_chassis_find_slot_with_chassis(struct PCIEChassis *c,
                                                     uint8_t slot)
{
    PCIESlot *s;
    QLIST_FOREACH(s, &c->slots, next) {
        if (s->slot == slot) {
            break;
        }
    }
    return s;
}

PCIESlot *pcie_chassis_find_slot(uint8_t chassis_number, uint16_t slot)
{
    struct PCIEChassis *c;
    c = pcie_chassis_find(chassis_number);
    if (!c) {
        return NULL;
    }
    return pcie_chassis_find_slot_with_chassis(c, slot);
}

int pcie_chassis_add_slot(struct PCIESlot *slot)
{
    struct PCIEChassis *c;
    c = pcie_chassis_find(slot->chassis);
    if (!c) {
        return -ENODEV;
    }
    if (pcie_chassis_find_slot_with_chassis(c, slot->slot)) {
        return -EBUSY;
    }
    QLIST_INSERT_HEAD(&c->slots, slot, next);
    return 0;
}

void pcie_chassis_del_slot(PCIESlot *s)
{
    QLIST_REMOVE(s, next);
}
