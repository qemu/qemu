/*
 * Neorv32-specific UART.
 *
 * Copyright (c) 2025 Michael Levit
 *
 * Author:
 *   Michael Levit <michael@videogpu.com>
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

#ifndef HW_NEORV32_UART_H
#define HW_NEORV32_UART_H

#include "chardev/char-fe.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_NEORV32_UART "riscv.neorv32.uart"
OBJECT_DECLARE_SIMPLE_TYPE(Neorv32UARTState, NEORV32_UART)

#define QEMU_UART_DATA_RX_FIFO_SIZE_LSB  8 /**256 < UART data register(8)  (r/-): log2(RX FIFO size), LSB */
#define QEMU_UART_DATA_RX_FIFO_SIZE_MSB  11 /** 2048 < UART data register(11) (r/-): log2(RX FIFO size), MSB */

#define NEORV32_UART_RX_FIFO_SIZE  32 //in HW it is  2048 + 256 = _MSB + _LSB

enum {
	NEORV32_UART_IE_TXWM       = 1, /* Transmit watermark interrupt enable */
	NEORV32_UART_IE_RXWM       = 2  /* Receive watermark interrupt enable */
};

enum {
	NEORV32_UART_IP_TXWM       = 1, /* Transmit watermark interrupt pending */
	NEORV32_UART_IP_RXWM       = 2  /* Receive watermark interrupt pending */
};



struct Neorv32UARTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq irq;
    MemoryRegion mmio;
    CharBackend chr;
    uint8_t rx_fifo[NEORV32_UART_RX_FIFO_SIZE];
    uint8_t rx_fifo_len;
    uint32_t ie; //interrupt enable
    uint32_t CTRL;
    uint32_t DATA;
};

Neorv32UARTState *neorv32_uart_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr);

#endif //HW_NEORV32_UART_H
