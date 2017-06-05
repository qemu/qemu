/*
 * STM32F2XX USART
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

#ifndef HW_STM32F2XX_USART_H
#define HW_STM32F2XX_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"

#define USART_SR   0x00
#define USART_DR   0x04
#define USART_BRR  0x08
#define USART_CR1  0x0C
#define USART_CR2  0x10
#define USART_CR3  0x14
#define USART_GTPR 0x18

#define USART_SR_RESET 0x00C00000

#define USART_SR_TXE  (1 << 7)
#define USART_SR_TC   (1 << 6)
#define USART_SR_RXNE (1 << 5)

#define USART_CR1_UE  (1 << 13)
#define USART_CR1_RXNEIE  (1 << 5)
#define USART_CR1_TE  (1 << 3)
#define USART_CR1_RE  (1 << 2)

#define TYPE_STM32F2XX_USART "stm32f2xx-usart"
#define STM32F2XX_USART(obj) \
    OBJECT_CHECK(STM32F2XXUsartState, (obj), TYPE_STM32F2XX_USART)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t usart_sr;
    uint32_t usart_dr;
    uint32_t usart_brr;
    uint32_t usart_cr1;
    uint32_t usart_cr2;
    uint32_t usart_cr3;
    uint32_t usart_gtpr;

    CharBackend chr;
    qemu_irq irq;
} STM32F2XXUsartState;
#endif /* HW_STM32F2XX_USART_H */
