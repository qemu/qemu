/*
 * STM32F4XX EXTI
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

#ifndef HW_STM_EXTI_H
#define HW_STM_EXTI_H

#include "hw/sysbus.h"
#include "hw/hw.h"

#define EXTI_IMR   0x00
#define EXTI_EMR   0x04
#define EXTI_RTSR  0x08
#define EXTI_FTSR  0x0C
#define EXTI_SWIER 0x10
#define EXTI_PR    0x14

#define TYPE_STM32F4XX_EXTI "stm32f4xx-exti"
#define STM32F4XX_EXTI(obj) \
    OBJECT_CHECK(STM32F4xxExtiState, (obj), TYPE_STM32F4XX_EXTI)

#define NUM_GPIO_EVENT_IN_LINES 16
#define NUM_INTERRUPT_OUT_LINES 16

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t exti_imr;
    uint32_t exti_emr;
    uint32_t exti_rtsr;
    uint32_t exti_ftsr;
    uint32_t exti_swier;
    uint32_t exti_pr;

    qemu_irq irq[NUM_INTERRUPT_OUT_LINES];
} STM32F4xxExtiState;

#endif
