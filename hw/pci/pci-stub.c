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
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"

bool msi_nonbroken;
bool pci_available;

PciInfoList *qmp_query_pci(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
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
