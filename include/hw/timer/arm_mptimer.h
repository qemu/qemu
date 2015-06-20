/*
 * Private peripheral timer/watchdog blocks for ARM 11MPCore and A9MP
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited
 * Written by Paul Brook, Peter Maydell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_TIMER_ARM_MPTIMER_H
#define HW_TIMER_ARM_MPTIMER_H

#include "hw/sysbus.h"

#define ARM_MPTIMER_MAX_CPUS 4

/* State of a single timer or watchdog block */
typedef struct {
    uint32_t count;
    uint32_t load;
    uint32_t control;
    uint32_t status;
    int64_t tick;
    QEMUTimer *timer;
    qemu_irq irq;
    MemoryRegion iomem;
} TimerBlock;

#define TYPE_ARM_MPTIMER "arm_mptimer"
#define ARM_MPTIMER(obj) \
    OBJECT_CHECK(ARMMPTimerState, (obj), TYPE_ARM_MPTIMER)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint32_t num_cpu;
    TimerBlock timerblock[ARM_MPTIMER_MAX_CPUS];
    MemoryRegion iomem;
} ARMMPTimerState;

#endif
