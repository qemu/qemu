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
#include "chardev/char-fe.h"
#include "qemu/timer.h"

#define IBEX_UART_INTR_STATE   0x00
    #define INTR_STATE_TX_WATERMARK (1 << 0)
    #define INTR_STATE_RX_WATERMARK (1 << 1)
    #define INTR_STATE_TX_EMPTY     (1 << 2)
    #define INTR_STATE_RX_OVERFLOW  (1 << 3)
#define IBEX_UART_INTR_ENABLE  0x04
#define IBEX_UART_INTR_TEST    0x08

#define IBEX_UART_CTRL         0x0c
    #define UART_CTRL_TX_ENABLE     (1 << 0)
    #define UART_CTRL_RX_ENABLE     (1 << 1)
    #define UART_CTRL_NF            (1 << 2)
    #define UART_CTRL_SLPBK         (1 << 4)
    #define UART_CTRL_LLPBK         (1 << 5)
    #define UART_CTRL_PARITY_EN     (1 << 6)
    #define UART_CTRL_PARITY_ODD    (1 << 7)
    #define UART_CTRL_RXBLVL        (3 << 8)
    #define UART_CTRL_NCO           (0xFFFF << 16)

#define IBEX_UART_STATUS       0x10
    #define UART_STATUS_TXFULL  (1 << 0)
    #define UART_STATUS_RXFULL  (1 << 1)
    #define UART_STATUS_TXEMPTY (1 << 2)
    #define UART_STATUS_RXIDLE  (1 << 4)
    #define UART_STATUS_RXEMPTY (1 << 5)

#define IBEX_UART_RDATA        0x14
#define IBEX_UART_WDATA        0x18

#define IBEX_UART_FIFO_CTRL    0x1c
    #define FIFO_CTRL_RXRST          (1 << 0)
    #define FIFO_CTRL_TXRST          (1 << 1)
    #define FIFO_CTRL_RXILVL         (7 << 2)
    #define FIFO_CTRL_RXILVL_SHIFT   (2)
    #define FIFO_CTRL_TXILVL         (3 << 5)
    #define FIFO_CTRL_TXILVL_SHIFT   (5)

#define IBEX_UART_FIFO_STATUS  0x20
#define IBEX_UART_OVRD         0x24
#define IBEX_UART_VAL          0x28
#define IBEX_UART_TIMEOUT_CTRL 0x2c

#define IBEX_UART_TX_FIFO_SIZE 16

#define TYPE_IBEX_UART "ibex-uart"
#define IBEX_UART(obj) \
    OBJECT_CHECK(IbexUartState, (obj), TYPE_IBEX_UART)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    uint8_t tx_fifo[IBEX_UART_TX_FIFO_SIZE];
    uint32_t tx_level;

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

    CharBackend chr;
    qemu_irq tx_watermark;
    qemu_irq rx_watermark;
    qemu_irq tx_empty;
    qemu_irq rx_overflow;
} IbexUartState;
#endif /* HW_IBEX_UART_H */
