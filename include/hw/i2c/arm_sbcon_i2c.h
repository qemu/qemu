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

#ifndef HW_I2C_ARM_SBCON_I2C_H
#define HW_I2C_ARM_SBCON_I2C_H

#include "hw/sysbus.h"
#include "hw/i2c/bitbang_i2c.h"
#include "qom/object.h"

#define TYPE_ARM_SBCON_I2C "versatile_i2c"

typedef struct ArmSbconI2CState ArmSbconI2CState;
DECLARE_INSTANCE_CHECKER(ArmSbconI2CState, ARM_SBCON_I2C, TYPE_ARM_SBCON_I2C)

struct ArmSbconI2CState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    bitbang_i2c_interface bitbang;
    int out;
    int in;
};

#endif /* HW_I2C_ARM_SBCON_I2C_H */
