/*
 * Device model for Cadence UART
 *
 * Copyright (c) 2010 Xilinx Inc.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written by Haibing Ma
 *            M.Habib
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CADENCE_UART_H
#define CADENCE_UART_H

#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define CADENCE_UART_RX_FIFO_SIZE           16
#define CADENCE_UART_TX_FIFO_SIZE           16

#define CADENCE_UART_R_MAX (0x48/4)

#define TYPE_CADENCE_UART "cadence_uart"
OBJECT_DECLARE_SIMPLE_TYPE(CadenceUARTState, CADENCE_UART)

struct CadenceUARTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t r[CADENCE_UART_R_MAX];
    uint8_t rx_fifo[CADENCE_UART_RX_FIFO_SIZE];
    uint8_t tx_fifo[CADENCE_UART_TX_FIFO_SIZE];
    uint32_t rx_wpos;
    uint32_t rx_count;
    uint32_t tx_count;
    uint64_t char_tx_time;
    CharBackend chr;
    qemu_irq irq;
    QEMUTimer *fifo_trigger_handle;
    Clock *refclk;
};

#endif
