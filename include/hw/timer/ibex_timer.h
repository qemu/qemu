/*
 * QEMU lowRISC Ibex Timer device
 *
 * Copyright (c) 2021 Western Digital
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

#ifndef HW_IBEX_TIMER_H
#define HW_IBEX_TIMER_H

#include "hw/sysbus.h"

#define TYPE_IBEX_TIMER "ibex-timer"
OBJECT_DECLARE_SIMPLE_TYPE(IbexTimerState, IBEX_TIMER)

struct IbexTimerState {
    /* <private> */
    SysBusDevice parent_obj;
    uint64_t mtimecmp;
    QEMUTimer *mtimer; /* Internal timer for M-mode interrupt */

    /* <public> */
    MemoryRegion mmio;

    uint32_t timer_ctrl;
    uint32_t timer_cfg0;
    uint32_t timer_compare_lower0;
    uint32_t timer_compare_upper0;
    uint32_t timer_intr_enable;
    uint32_t timer_intr_state;

    uint32_t timebase_freq;

    qemu_irq irq;

    qemu_irq m_timer_irq;
};
#endif /* HW_IBEX_TIMER_H */
