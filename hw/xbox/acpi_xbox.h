/*
 * QEMU Xbox PM Emulation
 *
 *  Copyright (c) 2012 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_ACPI_XBOX_H
#define HW_ACPI_XBOX_H

#include "hw/acpi/acpi.h"

typedef struct XBOX_PMRegs {
    ACPIREGS acpi_regs;

    MemoryRegion io;
    MemoryRegion io_gpio;

    qemu_irq irq;   
} XBOX_PMRegs;

void xbox_pm_init(PCIDevice *dev, XBOX_PMRegs *pm, qemu_irq sci_irq);
//void xbox_pm_iospace_update(MCPX_PMRegs *pm, uint32_t pm_io_base);


#endif