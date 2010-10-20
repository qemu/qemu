/*
 * QEMU PCI bridge
 *
 * Copyright (c) 2004 Fabrice Bellard
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * split out pci bus specific stuff from pci.[hc] to pci_bridge.[hc]
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 */

#ifndef QEMU_PCI_BRIDGE_H
#define QEMU_PCI_BRIDGE_H

#include "pci.h"

int pci_bridge_ssvid_init(PCIDevice *dev, uint8_t offset,
                          uint16_t svid, uint16_t ssid);

PCIDevice *pci_bridge_get_device(PCIBus *bus);
PCIBus *pci_bridge_get_sec_bus(PCIBridge *br);

pcibus_t pci_bridge_get_base(const PCIDevice *bridge, uint8_t type);
pcibus_t pci_bridge_get_limit(const PCIDevice *bridge, uint8_t type);

void pci_bridge_write_config(PCIDevice *d,
                             uint32_t address, uint32_t val, int len);
void pci_bridge_disable_base_limit(PCIDevice *dev);
void pci_bridge_reset_reg(PCIDevice *dev);
void pci_bridge_reset(DeviceState *qdev);

int pci_bridge_initfn(PCIDevice *pci_dev);
int pci_bridge_exitfn(PCIDevice *pci_dev);


/*
 * before qdev initialization(qdev_init()), this function sets bus_name and
 * map_irq callback which are necessry for pci_bridge_initfn() to
 * initialize bus.
 */
void pci_bridge_map_irq(PCIBridge *br, const char* bus_name,
                        pci_map_irq_fn map_irq);

#endif  /* QEMU_PCI_BRIDGE_H */
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 *  indent-tab-mode: nil
 * End:
 */
