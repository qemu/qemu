/*
 * ACPI implementation
 *
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
 /*
 *  Copyright (C) 2012 espes
 *
 *  Based on acpi.c, acpi_ich9.c, acpi_piix4.c
 */

#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "acpi.h"

#include "acpi_mcpx.h"

//#define DEBUG

#ifdef DEBUG
# define MCPX_DPRINTF(format, ...)     printf(format, ## __VA_ARGS__)
#else
# define MCPX_DPRINTF(format, ...)     do { } while (0)
#endif



static void mcpx_pm_update_sci_gn(ACPIREGS *regs)
{
    MCPX_PMRegs *pm = container_of(regs, MCPX_PMRegs, acpi_regs);
    //pm_update_sci(pm);
}


#define MCPX_PMIO_PM1_STS   0x0
#define MCPX_PMIO_PM1_EN    0x2
#define MCPX_PMIO_PM1_CNT   0x4
#define MCPX_PMIO_PM_TMR    0x8

static void mcpx_pm_ioport_write(IORange *ioport, uint64_t addr, unsigned width,
                                 uint64_t val)
{
    MCPX_PMRegs *pm = container_of(ioport, MCPX_PMRegs, ioport);

    switch (addr) {
        case MCPX_PMIO_PM1_STS:
            acpi_pm1_evt_write_sts(&pm->acpi_regs, val);
            //pm_update_sci(pm);
            break;
        case MCPX_PMIO_PM1_EN:
            pm->acpi_regs.pm1.evt.en = val;
            //pm_update_sci(pm);
            break;
        case MCPX_PMIO_PM1_CNT:
            acpi_pm1_cnt_write(&pm->acpi_regs, val, 0);
            break;
        default:
            break;
    }
    MCPX_DPRINTF("PM: write port=0x%04x val=0x%04x\n",
                 (unsigned int)addr, (unsigned int)val);
}

static void mcpx_pm_ioport_read(IORange *ioport, uint64_t addr, unsigned width,
                                uint64_t *data)
{
    MCPX_PMRegs *pm = container_of(ioport, MCPX_PMRegs, ioport);
    uint32_t val;

    switch (addr) {
        case MCPX_PMIO_PM1_STS:
            val = acpi_pm1_evt_get_sts(&pm->acpi_regs);
            break;
        case MCPX_PMIO_PM1_EN:
            val = pm->acpi_regs.pm1.evt.en;
            break;
        case MCPX_PMIO_PM1_CNT:
            val = pm->acpi_regs.pm1.cnt.cnt;
            break;
        case MCPX_PMIO_PM_TMR:
            val = acpi_pm_tmr_get(&pm->acpi_regs);
            break;
        default:
            val = 0;
            break;
    }
    MCPX_DPRINTF("PM: read port=0x%04x val=0x%04x\n",
                 (unsigned int)addr, (unsigned int)val);
    *data = val;
}

static const IORangeOps mcpx_iorange_ops = {
    .read = mcpx_pm_ioport_read,
    .write = mcpx_pm_ioport_write,
};


void mcpx_pm_iospace_update(MCPX_PMRegs *pm, uint32_t pm_io_base) {
    MCPX_DPRINTF("PM: iospace update to 0x%x\n", pm_io_base);

    //Disabled when 0
    if (pm_io_base != 0) {
        iorange_init(&pm->ioport, &mcpx_iorange_ops, pm_io_base, 256);
        ioport_register(&pm->ioport);
    }
}

void mcpx_pm_init(MCPX_PMRegs *pm/*, qemu_irq sci_irq*/) {
    acpi_pm_tmr_init(&pm->acpi_regs, mcpx_pm_update_sci_gn);
    acpi_pm1_cnt_init(&pm->acpi_regs);
    //acpi_gpe_init(&pm->acpi_regs, ICH9_PMIO_GPE0_LEN);

    //pm->irq = sci_irq;
}