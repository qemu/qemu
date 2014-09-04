/*
 * Global peripheral timer block for ARM A9MP
 *
 * (C) 2013 Xilinx Inc.
 *
 * Written by François LEGAL
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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

#ifndef HW_TIMER_A9_GTIMER_H_H
#define HW_TIMER_A9_GTIMER_H_H

#include "hw/sysbus.h"

#define A9_GTIMER_MAX_CPUS 4

#define TYPE_A9_GTIMER "arm.cortex-a9-global-timer"
#define A9_GTIMER(obj) OBJECT_CHECK(A9GTimerState, (obj), TYPE_A9_GTIMER)

#define R_COUNTER_LO                0x00
#define R_COUNTER_HI                0x04

#define R_CONTROL                   0x08
#define R_CONTROL_TIMER_ENABLE      (1 << 0)
#define R_CONTROL_COMP_ENABLE       (1 << 1)
#define R_CONTROL_IRQ_ENABLE        (1 << 2)
#define R_CONTROL_AUTO_INCREMENT    (1 << 2)
#define R_CONTROL_PRESCALER_SHIFT   8
#define R_CONTROL_PRESCALER_LEN     8
#define R_CONTROL_PRESCALER_MASK    (((1 << R_CONTROL_PRESCALER_LEN) - 1) << \
                                     R_CONTROL_PRESCALER_SHIFT)

#define R_CONTROL_BANKED            (R_CONTROL_COMP_ENABLE | \
                                     R_CONTROL_IRQ_ENABLE | \
                                     R_CONTROL_AUTO_INCREMENT)
#define R_CONTROL_NEEDS_SYNC        (R_CONTROL_TIMER_ENABLE | \
                                     R_CONTROL_PRESCALER_MASK)

#define R_INTERRUPT_STATUS          0x0C
#define R_COMPARATOR_LO             0x10
#define R_COMPARATOR_HI             0x14
#define R_AUTO_INCREMENT            0x18

typedef struct A9GTimerPerCPU A9GTimerPerCPU;
typedef struct A9GTimerState A9GTimerState;

struct A9GTimerPerCPU {
    A9GTimerState *parent;

    uint32_t control; /* only per cpu banked bits valid */
    uint64_t compare;
    uint32_t status;
    uint32_t inc;

    MemoryRegion iomem;
    qemu_irq irq; /* PPI interrupts */
};

struct A9GTimerState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    /* static props */
    uint32_t num_cpu;

    QEMUTimer *timer;

    uint64_t counter; /* current timer value */

    uint64_t ref_counter;
    uint64_t cpu_ref_time; /* the cpu time as of last update of ref_counter */
    uint32_t control; /* only non per cpu banked bits valid */

    A9GTimerPerCPU per_cpu[A9_GTIMER_MAX_CPUS];
};

typedef struct A9GTimerUpdate {
    uint64_t now;
    uint64_t new;
} A9GTimerUpdate;

#endif /* #ifdef HW_TIMER_A9_GTIMER_H_H */
