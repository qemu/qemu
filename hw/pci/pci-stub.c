/*
 * PCI stubs for platforms that don't support pci bus.
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

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "qapi/qapi-commands-pci.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"

bool msi_nonbroken;
bool pci_available;

PciInfoList *qmp_query_pci(Error **errp)
{
    return NULL;
}

void hmp_info_pci(Monitor *mon, const QDict *qdict)
{
}

void hmp_pcie_aer_inject_error(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "PCI devices not supported\n");
}

/* kvm-all wants this */
MSIMessage pci_get_msi_message(PCIDevice *dev, int vector)
{
    g_assert(false);
    return (MSIMessage){};
}

uint16_t pci_requester_id(PCIDevice *dev)
{
    g_assert(false);
    return 0;
}

/* Required by ahci.c */
bool msi_enabled(const PCIDevice *dev)
{
    return false;
}

void msi_notify(PCIDevice *dev, unsigned int vector)
{
    g_assert_not_reached();
}

/* Required by target/i386/kvm.c */
bool msi_is_masked(const PCIDevice *dev, unsigned vector)
{
    g_assert_not_reached();
}

MSIMessage msi_get_message(PCIDevice *dev, unsigned int vector)
{
    g_assert_not_reached();
}

int msix_enabled(PCIDevice *dev)
{
    return false;
}

bool msix_is_masked(PCIDevice *dev, unsigned vector)
{
    g_assert_not_reached();
}

MSIMessage msix_get_message(PCIDevice *dev, unsigned int vector)
{
    g_assert_not_reached();
}
