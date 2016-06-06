/*
 *  ASPEED AST2400 I2C Controller
 *
 *  Copyright (C) 2016 IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef ASPEED_I2C_H
#define ASPEED_I2C_H

#include "hw/i2c/i2c.h"

#define TYPE_ASPEED_I2C "aspeed.i2c"
#define ASPEED_I2C(obj) \
    OBJECT_CHECK(AspeedI2CState, (obj), TYPE_ASPEED_I2C)

#define ASPEED_I2C_NR_BUSSES 14

struct AspeedI2CState;

typedef struct AspeedI2CBus {
    struct AspeedI2CState *controller;

    MemoryRegion mr;

    I2CBus *bus;
    uint8_t id;

    uint32_t ctrl;
    uint32_t timing[2];
    uint32_t intr_ctrl;
    uint32_t intr_status;
    uint32_t cmd;
    uint32_t buf;
} AspeedI2CBus;

typedef struct AspeedI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t intr_status;

    AspeedI2CBus busses[ASPEED_I2C_NR_BUSSES];
} AspeedI2CState;

I2CBus *aspeed_i2c_get_bus(DeviceState *dev, int busnr);

#endif /* ASPEED_I2C_H */
