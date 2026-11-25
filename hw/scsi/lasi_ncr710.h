/*
 * LASI wrapper for the NCR 53C710 SCSI controller
 *
 * Copyright (c) 2025 Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>
 * Developed during Google Summer of Code 2025, mentored by
 * Helge Deller <deller@gmx.de>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_LASI_NCR710_H
#define HW_LASI_NCR710_H

#include "hw/core/sysbus.h"
#include "exec/memattrs.h"
#include "hw/scsi/scsi.h"
#include "hw/scsi/ncr53c710.h"

#define TYPE_LASI_NCR710 "lasi-ncr710"
OBJECT_DECLARE_SIMPLE_TYPE(LasiNCR710State, LASI_NCR710)

typedef struct LasiNCR710State {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq lasi_irq;       /* IRQ line to LASI controller */
    NCR710State ncr710;
} LasiNCR710State;

DeviceState *lasi_ncr710_init(MemoryRegion *addr_space, hwaddr hpa,
                               qemu_irq irq);
void lasi_ncr710_handle_legacy_cmdline(DeviceState *lasi_dev);

#endif
