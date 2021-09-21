/*
 * SiFive PWM
 *
 * Copyright (c) 2020 Western Digital
 *
 * Author:  Alistair Francis <alistair.francis@wdc.com>
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

#ifndef HW_SIFIVE_PWM_H
#define HW_SIFIVE_PWM_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_SIFIVE_PWM "sifive-pwm"

#define SIFIVE_PWM(obj) \
    OBJECT_CHECK(SiFivePwmState, (obj), TYPE_SIFIVE_PWM)

#define SIFIVE_PWM_CHANS          4
#define SIFIVE_PWM_IRQS           SIFIVE_PWM_CHANS

typedef struct SiFivePwmState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    QEMUTimer timer[SIFIVE_PWM_CHANS];
    /*
     * if en bit(s) set, is the number of ticks when pwmcount was 0
     * if en bit(s) not set, is the number of ticks in pwmcount
     */
    uint64_t tick_offset;
    uint64_t freq_hz;

    uint32_t pwmcfg;
    uint32_t pwmcmp[SIFIVE_PWM_CHANS];

    qemu_irq irqs[SIFIVE_PWM_IRQS];
} SiFivePwmState;

#endif /* HW_SIFIVE_PWM_H */
