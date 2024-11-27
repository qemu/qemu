/*
 * QEMU implementation of the Neorv32 SPI block.
 *
 * Copyright (c) 2025 Michael Levit.
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

#ifndef NEORV32_SPI_H
#define NEORV32_SPI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define TYPE_NEORV32_SPI "neorv32.spi"
#define NEORV32_SPI(obj) OBJECT_CHECK(NEORV32SPIState, (obj), TYPE_NEORV32_SPI)

typedef struct  NEORV32SPIState {
    SysBusDevice parent_obj;

    /* Memory-mapped registers */
    MemoryRegion mmio;

    /* IRQ line */
    qemu_irq irq;

    /* SPI bus (master) */
    SSIBus *bus;

    /* Chip selects (assume up to 3 CS lines) */
    qemu_irq *cs_lines;
    uint32_t num_cs;

    /* Registers:
     * Assume:
     * 0x00: CTRL (r/w)
     * 0x04: DATA (r/w)
     */
    uint32_t ctrl;
    uint32_t data;

    /* FIFOs */
    Fifo8 tx_fifo;
    Fifo8 rx_fifo;

    /* FIFO capacity */
    int fifo_capacity;
    /* Track CS state driven by command writes */
    bool cmd_cs_active;  /* true = CS asserted (active-low on wire) */
    int  current_cs;     /* which CS line is active; default 0 for now */
} NEORV32SPIState;



NEORV32SPIState *neorv32_spi_create(MemoryRegion *sys_mem, hwaddr base_addr);

#endif /* NEORV32_SPI_H */
