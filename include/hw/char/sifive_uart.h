/*
 * SiFive UART interface
 *
 * Copyright (c) 2016 Stefan O'Rear
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_UART_H
#define HW_SIFIVE_UART_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"
#include "qom/object.h"

enum {
    SIFIVE_UART_TXFIFO        = 0,
    SIFIVE_UART_RXFIFO        = 4,
    SIFIVE_UART_TXCTRL        = 8,
    SIFIVE_UART_TXMARK        = 10,
    SIFIVE_UART_RXCTRL        = 12,
    SIFIVE_UART_RXMARK        = 14,
    SIFIVE_UART_IE            = 16,
    SIFIVE_UART_IP            = 20,
    SIFIVE_UART_DIV           = 24,
    SIFIVE_UART_MAX           = 32
};

enum {
    SIFIVE_UART_IE_TXWM       = 1, /* Transmit watermark interrupt enable */
    SIFIVE_UART_IE_RXWM       = 2  /* Receive watermark interrupt enable */
};

enum {
    SIFIVE_UART_IP_TXWM       = 1, /* Transmit watermark interrupt pending */
    SIFIVE_UART_IP_RXWM       = 2  /* Receive watermark interrupt pending */
};

#define SIFIVE_UART_GET_TXCNT(txctrl)   ((txctrl >> 16) & 0x7)
#define SIFIVE_UART_GET_RXCNT(rxctrl)   ((rxctrl >> 16) & 0x7)

#define TYPE_SIFIVE_UART "riscv.sifive.uart"

typedef struct SiFiveUARTState SiFiveUARTState;
DECLARE_INSTANCE_CHECKER(SiFiveUARTState, SIFIVE_UART,
                         TYPE_SIFIVE_UART)

struct SiFiveUARTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq irq;
    MemoryRegion mmio;
    CharBackend chr;
    uint8_t rx_fifo[8];
    unsigned int rx_fifo_len;
    uint32_t ie;
    uint32_t ip;
    uint32_t txctrl;
    uint32_t rxctrl;
    uint32_t div;
};

SiFiveUARTState *sifive_uart_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr, qemu_irq irq);

#endif
