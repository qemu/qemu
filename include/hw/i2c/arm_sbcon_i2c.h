/*
 * ARM SBCon two-wire serial bus interface (I2C bitbang)
 *   a.k.a.
 * ARM Versatile I2C controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2012 Oskar Andero <oskar.andero@gmail.com>
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_I2C_ARM_SBCON_H
#define HW_I2C_ARM_SBCON_H

#include "hw/sysbus.h"
#include "hw/i2c/bitbang_i2c.h"

#define TYPE_VERSATILE_I2C "versatile_i2c"
#define TYPE_ARM_SBCON_I2C TYPE_VERSATILE_I2C

#define ARM_SBCON_I2C(obj) \
    OBJECT_CHECK(ArmSbconI2CState, (obj), TYPE_ARM_SBCON_I2C)

typedef struct ArmSbconI2CState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    bitbang_i2c_interface bitbang;
    int out;
    int in;
} ArmSbconI2CState;

#endif /* HW_I2C_ARM_SBCON_H */
