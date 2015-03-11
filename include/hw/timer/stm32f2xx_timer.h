/*
 * STM32F2XX Timer
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
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

#ifndef HW_STM32F2XX_TIMER_H
#define HW_STM32F2XX_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"

#define TIM_CR1      0x00
#define TIM_CR2      0x04
#define TIM_SMCR     0x08
#define TIM_DIER     0x0C
#define TIM_SR       0x10
#define TIM_EGR      0x14
#define TIM_CCMR1    0x18
#define TIM_CCMR2    0x1C
#define TIM_CCER     0x20
#define TIM_CNT      0x24
#define TIM_PSC      0x28
#define TIM_ARR      0x2C
#define TIM_CCR1     0x34
#define TIM_CCR2     0x38
#define TIM_CCR3     0x3C
#define TIM_CCR4     0x40
#define TIM_DCR      0x48
#define TIM_DMAR     0x4C
#define TIM_OR       0x50

#define TIM_CR1_CEN   1

#define TIM_EGR_UG 1

#define TIM_CCER_CC2E   (1 << 4)
#define TIM_CCMR1_OC2M2 (1 << 14)
#define TIM_CCMR1_OC2M1 (1 << 13)
#define TIM_CCMR1_OC2M0 (1 << 12)
#define TIM_CCMR1_OC2PE (1 << 11)

#define TIM_DIER_UIE  1

#define TYPE_STM32F2XX_TIMER "stm32f2xx-timer"
#define STM32F2XXTIMER(obj) OBJECT_CHECK(STM32F2XXTimerState, \
                            (obj), TYPE_STM32F2XX_TIMER)

typedef struct STM32F2XXTimerState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    int64_t tick_offset;
    uint64_t hit_time;
    uint64_t freq_hz;

    uint32_t tim_cr1;
    uint32_t tim_cr2;
    uint32_t tim_smcr;
    uint32_t tim_dier;
    uint32_t tim_sr;
    uint32_t tim_egr;
    uint32_t tim_ccmr1;
    uint32_t tim_ccmr2;
    uint32_t tim_ccer;
    uint32_t tim_psc;
    uint32_t tim_arr;
    uint32_t tim_ccr1;
    uint32_t tim_ccr2;
    uint32_t tim_ccr3;
    uint32_t tim_ccr4;
    uint32_t tim_dcr;
    uint32_t tim_dmar;
    uint32_t tim_or;
} STM32F2XXTimerState;

#endif /* HW_STM32F2XX_TIMER_H */
