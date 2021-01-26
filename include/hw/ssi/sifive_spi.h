/*
 * QEMU model of the SiFive SPI Controller
 *
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
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

#ifndef HW_SIFIVE_SPI_H
#define HW_SIFIVE_SPI_H

#define SIFIVE_SPI_REG_NUM  (0x78 / 4)

#define TYPE_SIFIVE_SPI "sifive.spi"
#define SIFIVE_SPI(obj) OBJECT_CHECK(SiFiveSPIState, (obj), TYPE_SIFIVE_SPI)

typedef struct SiFiveSPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t num_cs;
    qemu_irq *cs_lines;

    SSIBus *spi;

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;

    uint32_t regs[SIFIVE_SPI_REG_NUM];
} SiFiveSPIState;

#endif /* HW_SIFIVE_SPI_H */
