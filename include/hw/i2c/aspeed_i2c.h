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
#include "hw/sysbus.h"

#define TYPE_ASPEED_I2C "aspeed.i2c"
#define TYPE_ASPEED_2400_I2C TYPE_ASPEED_I2C "-ast2400"
#define TYPE_ASPEED_2500_I2C TYPE_ASPEED_I2C "-ast2500"
#define TYPE_ASPEED_2600_I2C TYPE_ASPEED_I2C "-ast2600"
#define ASPEED_I2C(obj) \
    OBJECT_CHECK(AspeedI2CState, (obj), TYPE_ASPEED_I2C)

#define ASPEED_I2C_NR_BUSSES 16
#define ASPEED_I2C_MAX_POOL_SIZE 0x800

struct AspeedI2CState;

typedef struct AspeedI2CBus {
    struct AspeedI2CState *controller;

    MemoryRegion mr;

    I2CBus *bus;
    uint8_t id;
    qemu_irq irq;

    uint32_t ctrl;
    uint32_t timing[2];
    uint32_t intr_ctrl;
    uint32_t intr_status;
    uint32_t cmd;
    uint32_t buf;
    uint32_t pool_ctrl;
    uint32_t dma_addr;
    uint32_t dma_len;
} AspeedI2CBus;

typedef struct AspeedI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t intr_status;
    uint32_t ctrl_global;
    MemoryRegion pool_iomem;
    uint8_t pool[ASPEED_I2C_MAX_POOL_SIZE];

    AspeedI2CBus busses[ASPEED_I2C_NR_BUSSES];
    MemoryRegion *dram_mr;
    AddressSpace dram_as;
} AspeedI2CState;

#define ASPEED_I2C_CLASS(klass) \
     OBJECT_CLASS_CHECK(AspeedI2CClass, (klass), TYPE_ASPEED_I2C)
#define ASPEED_I2C_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AspeedI2CClass, (obj), TYPE_ASPEED_I2C)

typedef struct AspeedI2CClass {
    SysBusDeviceClass parent_class;

    uint8_t num_busses;
    uint8_t reg_size;
    uint8_t gap;
    qemu_irq (*bus_get_irq)(AspeedI2CBus *);

    uint64_t pool_size;
    hwaddr pool_base;
    uint8_t *(*bus_pool_base)(AspeedI2CBus *);
    bool check_sram;
    bool has_dma;

} AspeedI2CClass;

I2CBus *aspeed_i2c_get_bus(DeviceState *dev, int busnr);

#endif /* ASPEED_I2C_H */
