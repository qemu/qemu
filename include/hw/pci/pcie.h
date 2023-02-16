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

#include "hw/pci/pci_regs.h"
#include "hw/pci/pcie_regs.h"
#include "hw/pci/pcie_aer.h"
#include "hw/pci/pcie_sriov.h"
#include "hw/hotplug.h"

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
    /* Offset of Power Management capability in config space */
    uint8_t pm_cap;

    /* SLOT */
    bool hpev_notified; /* Logical AND of conditions for hot plug event.
                         Following 6.7.3.4:
                         Software Notification of Hot-Plug Events, an interrupt
                         is sent whenever the logical and of these conditions
                         transitions from false to true. */

    /* AER */
    uint16_t aer_cap;
    PCIEAERLog aer_log;

    /* Offset of ATS capability in config space */
    uint16_t ats_cap;

    /* ACS */
    uint16_t acs_cap;

    /* SR/IOV */
    uint16_t sriov_cap;
    PCIESriovPF sriov_pf;
    PCIESriovVF sriov_vf;
};

#define COMPAT_PROP_PCP "power_controller_present"

/* PCI express capability helper functions */
int pcie_cap_init(PCIDevice *dev, uint8_t offset, uint8_t type,
                  uint8_t port, Error **errp);
int pcie_cap_v1_init(PCIDevice *dev, uint8_t offset,
                     uint8_t type, uint8_t port);
int pcie_endpoint_cap_init(PCIDevice *dev, uint8_t offset);
void pcie_cap_exit(PCIDevice *dev);
int pcie_endpoint_cap_v1_init(PCIDevice *dev, uint8_t offset);
void pcie_cap_v1_exit(PCIDevice *dev);
uint8_t pcie_cap_get_type(const PCIDevice *dev);
void pcie_cap_flags_set_vector(PCIDevice *dev, uint8_t vector);
uint8_t pcie_cap_flags_get_vector(PCIDevice *dev);

void pcie_cap_deverr_init(PCIDevice *dev);
void pcie_cap_deverr_reset(PCIDevice *dev);

void pcie_cap_lnkctl_init(PCIDevice *dev);
void pcie_cap_lnkctl_reset(PCIDevice *dev);

void pcie_cap_slot_init(PCIDevice *dev, PCIESlot *s);
void pcie_cap_slot_reset(PCIDevice *dev);
void pcie_cap_slot_get(PCIDevice *dev, uint16_t *slt_ctl, uint16_t *slt_sta);
void pcie_cap_slot_write_config(PCIDevice *dev,
                                uint16_t old_slt_ctl, uint16_t old_slt_sta,
                                uint32_t addr, uint32_t val, int len);
int pcie_cap_slot_post_load(void *opaque, int version_id);
void pcie_cap_slot_push_attention_button(PCIDevice *dev);
void pcie_cap_slot_enable_power(PCIDevice *dev);

void pcie_cap_root_init(PCIDevice *dev);
void pcie_cap_root_reset(PCIDevice *dev);

void pcie_cap_flr_init(PCIDevice *dev);
void pcie_cap_flr_write_config(PCIDevice *dev,
                           uint32_t addr, uint32_t val, int len);

/* ARI forwarding capability and control */
void pcie_cap_arifwd_init(PCIDevice *dev);
void pcie_cap_arifwd_reset(PCIDevice *dev);
bool pcie_cap_is_arifwd_enabled(const PCIDevice *dev);

/* PCI express extended capability helper functions */
uint16_t pcie_find_capability(PCIDevice *dev, uint16_t cap_id);
void pcie_add_capability(PCIDevice *dev,
                         uint16_t cap_id, uint8_t cap_ver,
                         uint16_t offset, uint16_t size);
void pcie_sync_bridge_lnk(PCIDevice *dev);

void pcie_acs_init(PCIDevice *dev, uint16_t offset);
void pcie_acs_reset(PCIDevice *dev);

void pcie_ari_init(PCIDevice *dev, uint16_t offset, uint16_t nextfn);
void pcie_dev_ser_num_init(PCIDevice *dev, uint16_t offset, uint64_t ser_num);
void pcie_ats_init(PCIDevice *dev, uint16_t offset, bool aligned);

void pcie_cap_slot_pre_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp);
void pcie_cap_slot_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                           Error **errp);
void pcie_cap_slot_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                             Error **errp);
void pcie_cap_slot_unplug_request_cb(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp);
#endif /* QEMU_PCIE_H */
