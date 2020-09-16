/*
 * ARM SSE-200 CPU_IDENTITY register block
 *
 * Copyright (c) 2019 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "CPU_IDENTITY" register block which is part of the
 * Arm SSE-200 and documented in
 * http://infocenter.arm.com/help/topic/com.arm.doc.101104_0100_00_en/corelink_sse200_subsystem_for_embedded_technical_reference_manual_101104_0100_00_en.pdf
 *
 * QEMU interface:
 *  + QOM property "CPUID": the value to use for the CPUID register
 *  + sysbus MMIO region 0: the system information register bank
 */

#ifndef HW_MISC_ARMSSE_CPUID_H
#define HW_MISC_ARMSSE_CPUID_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ARMSSE_CPUID "armsse-cpuid"
OBJECT_DECLARE_SIMPLE_TYPE(ARMSSECPUID, ARMSSE_CPUID)

struct ARMSSECPUID {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* Properties */
    uint32_t cpuid;
};

#endif
