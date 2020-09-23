/*
 * ARM11MPCore internal peripheral emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#ifndef HW_CPU_ARM11MPCORE_H
#define HW_CPU_ARM11MPCORE_H

#include "hw/sysbus.h"
#include "hw/misc/arm11scu.h"
#include "hw/intc/arm_gic.h"
#include "hw/timer/arm_mptimer.h"
#include "qom/object.h"

#define TYPE_ARM11MPCORE_PRIV "arm11mpcore_priv"
OBJECT_DECLARE_SIMPLE_TYPE(ARM11MPCorePriveState, ARM11MPCORE_PRIV)

struct ARM11MPCorePriveState {
    SysBusDevice parent_obj;

    uint32_t num_cpu;
    MemoryRegion container;
    uint32_t num_irq;

    ARM11SCUState scu;
    GICState gic;
    ARMMPTimerState mptimer;
    ARMMPTimerState wdtimer;
};

#endif
