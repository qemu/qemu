/*
 *  Allwinner SPI Bus Serial Interface registers definition
 *
 *  Copyright (C) 2024 Strahinja Jankovic. <strahinja.p.jankovic@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ALLWINNER_A10_SPI_H
#define ALLWINNER_A10_SPI_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

/** Size of register I/O address space used by SPI device */
#define AW_A10_SPI_IOSIZE (0x1000)

/** Total number of known registers */
#define AW_A10_SPI_REGS_NUM    (AW_A10_SPI_IOSIZE / sizeof(uint32_t))
#define AW_A10_SPI_FIFO_SIZE   (64)
#define AW_A10_SPI_CS_LINES_NR (4)

#define TYPE_AW_A10_SPI        "allwinner.spi"
OBJECT_DECLARE_SIMPLE_TYPE(AWA10SPIState, AW_A10_SPI)

struct AWA10SPIState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    SSIBus *bus;
    qemu_irq irq;
    qemu_irq cs_lines[AW_A10_SPI_CS_LINES_NR];

    uint32_t regs[AW_A10_SPI_REGS_NUM];

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
};

#endif /* ALLWINNER_A10_SPI_H */
