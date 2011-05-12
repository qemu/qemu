/*
 * pcie.h
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

#ifndef QEMU_PCIE_H
#define QEMU_PCIE_H

#include "hw.h"
#include "pci_regs.h"
#include "pcie_regs.h"
#include "pcie_aer.h"

typedef enum {
    /* for attention and power indicator */
    PCI_EXP_HP_IND_RESERVED     = PCI_EXP_SLTCTL_IND_RESERVED,
    PCI_EXP_HP_IND_ON           = PCI_EXP_SLTCTL_IND_ON,
    PCI_EXP_HP_IND_BLINK        = PCI_EXP_SLTCTL_IND_BLINK,
    PCI_EXP_HP_IND_OFF          = PCI_EXP_SLTCTL_IND_OFF,
} PCIExpressIndicator;

typedef enum {
    /* these bits must match the bits in Slot Control/Status registers.
     * PCI_EXP_HP_EV_xxx = PCI_EXP_SLTCTL_xxxE = PCI_EXP_SLTSTA_xxx
     *
     * Not all the bits of slot control register match with the ones of
     * slot status. Not some bits of slot status register is used to
     * show status, not to report event occurrence.
     * So such bits must be masked out when checking the software
     * notification condition.
     */
    PCI_EXP_HP_EV_ABP           = PCI_EXP_SLTCTL_ABPE,
                                        /* attention button pressed */
    PCI_EXP_HP_EV_PDC           = PCI_EXP_SLTCTL_PDCE,
                                        /* presence detect changed */
    PCI_EXP_HP_EV_CCI           = PCI_EXP_SLTCTL_CCIE,
                                        /* command completed */

    PCI_EXP_HP_EV_SUPPORTED     = PCI_EXP_HP_EV_ABP |
                                  PCI_EXP_HP_EV_PDC |
                                  PCI_EXP_HP_EV_CCI,
                                                /* supported event mask  */

    /* events not listed aren't supported */
} PCIExpressHotPlugEvent;

struct PCIExpressDevice {
    /* Offset of express capability in config space */
    uint8_t exp_cap;

    /* SLOT */
    unsigned int hpev_intx;     /* INTx for hot plug event (0-3:INT[A-D]#)
                                 * default is 0 = INTA#
                                 * If the chip wants to use other interrupt
                                 * line, initialize this member with the
                                 * desired number.
                                 * If the chip dynamically changes this member,
                                 * also initialize it when loaded as
                                 * appropreately.
                                 */
    bool hpev_notified; /* Logical AND of conditions for hot plug event.
                         Following 6.7.3.4:
                         Software Notification of Hot-Plug Events, an interrupt
                         is sent whenever the logical and of these conditions
                         transitions from false to true. */

    /* AER */
    uint16_t aer_cap;
    PCIEAERLog aer_log;
    unsigned int aer_intx;      /* INTx for error reporting
                                 * default is 0 = INTA#
                                 * If the chip wants to use other interrupt
                                 * line, initialize this member with the
                                 * desired number.
                                 * If the chip dynamically changes this member,
                                 * also initialize it when loaded as
                                 * appropreately.
                                 */
};

/* PCI express capability helper functions */
int pcie_cap_init(PCIDevice *dev, uint8_t offset, uint8_t type, uint8_t port);
void pcie_cap_exit(PCIDevice *dev);
uint8_t pcie_cap_get_type(const PCIDevice *dev);
void pcie_cap_flags_set_vector(PCIDevice *dev, uint8_t vector);
uint8_t pcie_cap_flags_get_vector(PCIDevice *dev);

void pcie_cap_deverr_init(PCIDevice *dev);
void pcie_cap_deverr_reset(PCIDevice *dev);

void pcie_cap_slot_init(PCIDevice *dev, uint16_t slot);
void pcie_cap_slot_reset(PCIDevice *dev);
void pcie_cap_slot_write_config(PCIDevice *dev,
                                uint32_t addr, uint32_t val, int len);
int pcie_cap_slot_post_load(void *opaque, int version_id);
void pcie_cap_slot_push_attention_button(PCIDevice *dev);

void pcie_cap_root_init(PCIDevice *dev);
void pcie_cap_root_reset(PCIDevice *dev);

void pcie_cap_flr_init(PCIDevice *dev);
void pcie_cap_flr_write_config(PCIDevice *dev,
                           uint32_t addr, uint32_t val, int len);

void pcie_cap_ari_init(PCIDevice *dev);
void pcie_cap_ari_reset(PCIDevice *dev);
bool pcie_cap_is_ari_enabled(const PCIDevice *dev);

/* PCI express extended capability helper functions */
uint16_t pcie_find_capability(PCIDevice *dev, uint16_t cap_id);
void pcie_add_capability(PCIDevice *dev,
                         uint16_t cap_id, uint8_t cap_ver,
                         uint16_t offset, uint16_t size);

void pcie_ari_init(PCIDevice *dev, uint16_t offset, uint16_t nextfn);

#endif /* QEMU_PCIE_H */
