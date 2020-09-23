/*
 * Cortex-A9MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */
#ifndef HW_CPU_A9MPCORE_H
#define HW_CPU_A9MPCORE_H

#include "hw/sysbus.h"
#include "hw/intc/arm_gic.h"
#include "hw/misc/a9scu.h"
#include "hw/timer/arm_mptimer.h"
#include "hw/timer/a9gtimer.h"
#include "qom/object.h"

#define TYPE_A9MPCORE_PRIV "a9mpcore_priv"
OBJECT_DECLARE_SIMPLE_TYPE(A9MPPrivState, A9MPCORE_PRIV)

struct A9MPPrivState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint32_t num_cpu;
    MemoryRegion container;
    uint32_t num_irq;

    A9SCUState scu;
    GICState gic;
    A9GTimerState gtimer;
    ARMMPTimerState mptimer;
    ARMMPTimerState wdt;
};

#endif
