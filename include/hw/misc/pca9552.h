/*
 * PCA9552 I2C LED blinker
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#ifndef PCA9552_H
#define PCA9552_H

#include "hw/i2c/i2c.h"

#define TYPE_PCA9552 "pca9552"
#define PCA9552(obj) OBJECT_CHECK(PCA9552State, (obj), TYPE_PCA9552)

#define PCA9552_NR_REGS 10

typedef struct PCA9552State {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/

    uint8_t len;
    uint8_t pointer;

    uint8_t regs[PCA9552_NR_REGS];
    uint8_t max_reg;
    uint8_t nr_leds;
} PCA9552State;

#endif
