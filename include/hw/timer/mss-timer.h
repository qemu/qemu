/*
 * Microsemi SmartFusion2 Timer.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_MSS_TIMER_H
#define HW_MSS_TIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qom/object.h"

#define TYPE_MSS_TIMER     "mss-timer"
OBJECT_DECLARE_SIMPLE_TYPE(MSSTimerState, MSS_TIMER)

/*
 * There are two 32-bit down counting timers.
 * Timers 1 and 2 can be concatenated into a single 64-bit Timer
 * that operates either in Periodic mode or in One-shot mode.
 * Writing 1 to the TIM64_MODE register bit 0 sets the Timers in 64-bit mode.
 * In 64-bit mode, writing to the 32-bit registers has no effect.
 * Similarly, in 32-bit mode, writing to the 64-bit mode registers
 * has no effect. Only two 32-bit timers are supported currently.
 */
#define NUM_TIMERS        2

#define R_TIM1_MAX        6

struct Msf2Timer {
    ptimer_state *ptimer;

    uint32_t regs[R_TIM1_MAX];
    qemu_irq irq;
};

struct MSSTimerState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t freq_hz;
    struct Msf2Timer timers[NUM_TIMERS];
};

#endif /* HW_MSS_TIMER_H */
