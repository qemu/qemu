/*
 * STM32F2XX ADC
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

#ifndef HW_STM32F2XX_ADC_H
#define HW_STM32F2XX_ADC_H

#include "hw/sysbus.h"

#define ADC_SR    0x00
#define ADC_CR1   0x04
#define ADC_CR2   0x08
#define ADC_SMPR1 0x0C
#define ADC_SMPR2 0x10
#define ADC_JOFR1 0x14
#define ADC_JOFR2 0x18
#define ADC_JOFR3 0x1C
#define ADC_JOFR4 0x20
#define ADC_HTR   0x24
#define ADC_LTR   0x28
#define ADC_SQR1  0x2C
#define ADC_SQR2  0x30
#define ADC_SQR3  0x34
#define ADC_JSQR  0x38
#define ADC_JDR1  0x3C
#define ADC_JDR2  0x40
#define ADC_JDR3  0x44
#define ADC_JDR4  0x48
#define ADC_DR    0x4C

#define ADC_CR2_ADON    0x01
#define ADC_CR2_CONT    0x02
#define ADC_CR2_ALIGN   0x800
#define ADC_CR2_SWSTART 0x40000000

#define ADC_CR1_RES 0x3000000

#define ADC_COMMON_ADDRESS 0x100

#define TYPE_STM32F2XX_ADC "stm32f2xx-adc"
#define STM32F2XX_ADC(obj) \
    OBJECT_CHECK(STM32F2XXADCState, (obj), TYPE_STM32F2XX_ADC)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t adc_sr;
    uint32_t adc_cr1;
    uint32_t adc_cr2;
    uint32_t adc_smpr1;
    uint32_t adc_smpr2;
    uint32_t adc_jofr[4];
    uint32_t adc_htr;
    uint32_t adc_ltr;
    uint32_t adc_sqr1;
    uint32_t adc_sqr2;
    uint32_t adc_sqr3;
    uint32_t adc_jsqr;
    uint32_t adc_jdr[4];
    uint32_t adc_dr;

    qemu_irq irq;
} STM32F2XXADCState;

#endif /* HW_STM32F2XX_ADC_H */
