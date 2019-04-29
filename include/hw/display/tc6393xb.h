/*
 * Toshiba TC6393XB I/O Controller.
 * Found in Sharp Zaurus SL-6000 (tosa) or some
 * Toshiba e-Series PDAs.
 *
 * Copyright (c) 2007 Hervé Poussineau
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_DISPLAY_TC6393XB_H
#define HW_DISPLAY_TC6393XB_H

#include "exec/memory.h"
#include "hw/irq.h"

typedef struct TC6393xbState TC6393xbState;

TC6393xbState *tc6393xb_init(struct MemoryRegion *sysmem,
                             uint32_t base, qemu_irq irq);
qemu_irq tc6393xb_l3v_get(TC6393xbState *s);

#endif
