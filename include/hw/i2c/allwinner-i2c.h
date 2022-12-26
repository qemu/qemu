/*
 *  Allwinner I2C Bus Serial Interface registers definition
 *
 *  Copyright (C) 2022 Strahinja Jankovic. <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from IMX I2C controller,
 *  by Jean-Christophe DUBOIS .
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
 */

#ifndef ALLWINNER_I2C_H
#define ALLWINNER_I2C_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_AW_I2C "allwinner.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(AWI2CState, AW_I2C)

#define AW_I2C_MEM_SIZE         0x24

struct AWI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint8_t addr;
    uint8_t xaddr;
    uint8_t data;
    uint8_t cntr;
    uint8_t stat;
    uint8_t ccr;
    uint8_t srst;
    uint8_t efr;
    uint8_t lcr;
};

#endif /* ALLWINNER_I2C_H */
