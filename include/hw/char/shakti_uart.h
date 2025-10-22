/*
 * SHAKTI UART
 *
 * Copyright (c) 2021 Vijai Kumar K <vijai@behindbytes.com>
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

#ifndef HW_SHAKTI_UART_H
#define HW_SHAKTI_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define SHAKTI_UART_BAUD        0x00
#define SHAKTI_UART_TX          0x04
#define SHAKTI_UART_RX          0x08
#define SHAKTI_UART_STATUS      0x0C
#define SHAKTI_UART_DELAY       0x10
#define SHAKTI_UART_CONTROL     0x14
#define SHAKTI_UART_INT_EN      0x18
#define SHAKTI_UART_IQ_CYCLES   0x1C
#define SHAKTI_UART_RX_THRES    0x20

#define SHAKTI_UART_STATUS_TX_EMPTY     (1 << 0)
#define SHAKTI_UART_STATUS_TX_FULL      (1 << 1)
#define SHAKTI_UART_STATUS_RX_NOT_EMPTY (1 << 2)
#define SHAKTI_UART_STATUS_RX_FULL      (1 << 3)
/* 9600 8N1 is the default setting */
/* Reg value = (50000000 Hz)/(16 * 9600)*/
#define SHAKTI_UART_BAUD_DEFAULT    0x0145
#define SHAKTI_UART_CONTROL_DEFAULT 0x0100

#define TYPE_SHAKTI_UART "shakti-uart"
#define SHAKTI_UART(obj) \
    OBJECT_CHECK(ShaktiUartState, (obj), TYPE_SHAKTI_UART)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint32_t uart_baud;
    uint32_t uart_tx;
    uint32_t uart_rx;
    uint32_t uart_status;
    uint32_t uart_delay;
    uint32_t uart_control;
    uint32_t uart_interrupt;
    uint32_t uart_iq_cycles;
    uint32_t uart_rx_threshold;

    CharFrontend chr;
} ShaktiUartState;

#endif /* HW_SHAKTI_UART_H */
