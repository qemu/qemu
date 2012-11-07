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

#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "acpi.h"
#include "xbox_pci.h"
#include "acpi_xbox.h"

//#define DEBUG
#ifdef DEBUG
# define XBOX_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define XBOX_DPRINTF(format, ...)     do { } while (0)
#endif



static void xbox_pm_update_sci_gn(ACPIREGS *regs)
{
    XBOX_PMRegs *pm = container_of(regs, XBOX_PMRegs, acpi_regs);
    //pm_update_sci(pm);
}


#define XBOX_PMIO_PM1_STS   0x0
#define XBOX_PMIO_PM1_EN    0x2
#define XBOX_PMIO_PM1_CNT   0x4
#define XBOX_PMIO_PM_TMR    0x8

static void xbox_pm_ioport_write(void *opaque,
                                 hwaddr addr,
                                 uint64_t val, unsigned size)
{
    XBOX_PMRegs *pm = opaque;

    switch (addr) {
        case XBOX_PMIO_PM1_STS:
            acpi_pm1_evt_write_sts(&pm->acpi_regs, val);
            //pm_update_sci(pm);
            break;
        case XBOX_PMIO_PM1_EN:
            pm->acpi_regs.pm1.evt.en = val;
            //pm_update_sci(pm);
            break;
        case XBOX_PMIO_PM1_CNT:
            acpi_pm1_cnt_write(&pm->acpi_regs, val, 0);
            break;
        default:
            break;
    }
    XBOX_DPRINTF("PM: write port=0x%04x val=0x%04x\n",
                 (unsigned int)addr, (unsigned int)val);
}

static uint64_t xbox_pm_ioport_read(void *opaque,
                                    hwaddr addr,
                                    unsigned size)
{
    XBOX_PMRegs *pm = opaque;
    uint64_t val;

    switch (addr) {
        case XBOX_PMIO_PM1_STS:
            val = acpi_pm1_evt_get_sts(&pm->acpi_regs);
            break;
        case XBOX_PMIO_PM1_EN:
            val = pm->acpi_regs.pm1.evt.en;
            break;
        case XBOX_PMIO_PM1_CNT:
            val = pm->acpi_regs.pm1.cnt.cnt;
            break;
        case XBOX_PMIO_PM_TMR:
            val = acpi_pm_tmr_get(&pm->acpi_regs);
            break;
        default:
            val = 0;
            break;
    }
    XBOX_DPRINTF("PM: read port=0x%04x val=0x%04x\n",
                 (unsigned int)addr, (unsigned int)val);
    return val;
}

static const MemoryRegionOps xbox_pm_ops = {
    .read = xbox_pm_ioport_read,
    .write = xbox_pm_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

#if 0
void xbox_pm_iospace_update(XBOX_PMRegs *pm, uint32_t pm_io_base) {
    XBOX_DPRINTF("PM: iospace update to 0x%x\n", pm_io_base);

    //Disabled when 0
    if (pm_io_base != 0) {
        iorange_init(&pm->ioport, &xbox_iorange_ops, pm_io_base, 256);
        ioport_register(&pm->ioport);
    }
}
#endif


#define XBOX_PM_BASE_BAR 0

void xbox_pm_init(PCIDevice *dev, XBOX_PMRegs *pm/*, qemu_irq sci_irq*/) {

    memory_region_init_io(&pm->bar, &xbox_pm_ops,
                          pm, "xbox-pm-bar", 256);
    pci_register_bar(dev, XBOX_PM_BASE_BAR, PCI_BASE_ADDRESS_SPACE_IO,
                     &pm->bar);

    acpi_pm_tmr_init(&pm->acpi_regs, xbox_pm_update_sci_gn);
    acpi_pm1_cnt_init(&pm->acpi_regs);
    //acpi_gpe_init(&pm->acpi_regs, ICH9_PMIO_GPE0_LEN);

    //pm->irq = sci_irq;
}