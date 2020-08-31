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
#include "qom/object.h"

#define TYPE_PCA9552 "pca9552"
#define TYPE_PCA955X "pca955x"
typedef struct PCA955xState PCA955xState;
DECLARE_INSTANCE_CHECKER(PCA955xState, PCA955X,
                         TYPE_PCA955X)

#define PCA955X_NR_REGS 10
#define PCA955X_PIN_COUNT_MAX 16

struct PCA955xState {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/

    uint8_t len;
    uint8_t pointer;

    uint8_t regs[PCA955X_NR_REGS];
    qemu_irq gpio[PCA955X_PIN_COUNT_MAX];
    char *description; /* For debugging purpose only */
};

#endif
