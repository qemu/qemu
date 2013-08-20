/*
 * Xbox ACPI implementation
 *
 * Copyright (c) 2012 espes
 *
 * Based on acpi.c, acpi_ich9.c, acpi_piix4.c
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 * Copyright (c) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/acpi/acpi.h"
#include "hw/xbox_pci.h"

#include "hw/acpi_xbox.h"

//#define DEBUG
#ifdef DEBUG
# define XBOX_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define XBOX_DPRINTF(format, ...)     do { } while (0)
#endif



static void xbox_pm_update_sci_fn(ACPIREGS *regs)
{
    //XBOX_PMRegs *pm = container_of(regs, XBOX_PMRegs, acpi_regs);
    //pm_update_sci(pm);
}


#define XBOX_PM_BASE_BAR 0

void xbox_pm_init(PCIDevice *dev, XBOX_PMRegs *pm/*, qemu_irq sci_irq*/) {

    memory_region_init(&pm->bar, OBJECT(dev), "xbox-pm-bar", 256);
    pci_register_bar(dev, XBOX_PM_BASE_BAR, PCI_BASE_ADDRESS_SPACE_IO,
                     &pm->bar);


    acpi_pm_tmr_init(&pm->acpi_regs, xbox_pm_update_sci_fn, &pm->bar);
    acpi_pm1_evt_init(&pm->acpi_regs, xbox_pm_update_sci_fn, &pm->bar);
    acpi_pm1_cnt_init(&pm->acpi_regs, &pm->bar, 2);

    //pm->irq = sci_irq;
}