/*
 * QEMU GMCH/ICH9 LPC PM Emulation
 *
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
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

#ifndef HW_ACPI_ICH9_H
#define HW_ACPI_ICH9_H

#include "hw/acpi/acpi.h"

typedef struct ICH9LPCPMRegs {
    /*
     * In ich9 spec says that pm1_cnt register is 32bit width and
     * that the upper 16bits are reserved and unused.
     * PM1a_CNT_BLK = 2 in FADT so it is defined as uint16_t.
     */
    ACPIREGS acpi_regs;

    MemoryRegion io;
    MemoryRegion io_gpe;
    MemoryRegion io_smi;

    uint32_t smi_en;
    uint32_t smi_sts;

    qemu_irq irq;      /* SCI */

    uint32_t pm_io_base;
    Notifier powerdown_notifier;
} ICH9LPCPMRegs;

void ich9_pm_init(PCIDevice *lpc_pci, ICH9LPCPMRegs *pm,
                  qemu_irq sci_irq);
void ich9_pm_iospace_update(ICH9LPCPMRegs *pm, uint32_t pm_io_base);
extern const VMStateDescription vmstate_ich9_pm;

void ich9_pm_add_properties(Object *obj, ICH9LPCPMRegs *pm, Error **errp);

#endif /* HW_ACPI_ICH9_H */
