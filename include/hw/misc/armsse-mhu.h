/*
 * ARM SSE-200 Message Handling Unit (MHU)
 *
 * Copyright (c) 2019 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the Message Handling Unit (MHU) which is part of the
 * Arm SSE-200 and documented in
 * http://infocenter.arm.com/help/topic/com.arm.doc.101104_0100_00_en/corelink_sse200_subsystem_for_embedded_technical_reference_manual_101104_0100_00_en.pdf
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the system information register bank
 *  + sysbus IRQ 0: interrupt for CPU 0
 *  + sysbus IRQ 1: interrupt for CPU 1
 */

#ifndef HW_MISC_ARMSSE_MHU_H
#define HW_MISC_ARMSSE_MHU_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ARMSSE_MHU "armsse-mhu"
OBJECT_DECLARE_SIMPLE_TYPE(ARMSSEMHU, ARMSSE_MHU)

struct ARMSSEMHU {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq cpu0irq;
    qemu_irq cpu1irq;

    uint32_t cpu0intr;
    uint32_t cpu1intr;
};

#endif
