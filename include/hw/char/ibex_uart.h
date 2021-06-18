/*
 * QEMU lowRISC Ibex UART device
 *
 * Copyright (c) 2020 Western Digital
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

#ifndef HW_IBEX_UART_H
#define HW_IBEX_UART_H

#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define IBEX_UART_TX_FIFO_SIZE 16
#define IBEX_UART_CLOCK 50000000 /* 50MHz clock */

#define TYPE_IBEX_UART "ibex-uart"
OBJECT_DECLARE_SIMPLE_TYPE(IbexUartState, IBEX_UART)

struct IbexUartState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint8_t tx_fifo[IBEX_UART_TX_FIFO_SIZE];
    uint32_t tx_level;

    uint32_t rx_level;

    QEMUTimer *fifo_trigger_handle;
    uint64_t char_tx_time;

    uint32_t uart_intr_state;
    uint32_t uart_intr_enable;
    uint32_t uart_ctrl;
    uint32_t uart_status;
    uint32_t uart_rdata;
    uint32_t uart_fifo_ctrl;
    uint32_t uart_fifo_status;
    uint32_t uart_ovrd;
    uint32_t uart_val;
    uint32_t uart_timeout_ctrl;

    Clock *f_clk;

    CharBackend chr;
    qemu_irq tx_watermark;
    qemu_irq rx_watermark;
    qemu_irq tx_empty;
    qemu_irq rx_overflow;
};
#endif /* HW_IBEX_UART_H */
