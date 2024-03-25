/*
 * PCA9554 I/O port
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PCA9554_H
#define PCA9554_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_PCA9554 "pca9554"
typedef struct PCA9554State PCA9554State;
DECLARE_INSTANCE_CHECKER(PCA9554State, PCA9554,
                         TYPE_PCA9554)

#define PCA9554_NR_REGS 4
#define PCA9554_PIN_COUNT 8

struct PCA9554State {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/

    uint8_t len;
    uint8_t pointer;

    uint8_t regs[PCA9554_NR_REGS];
    qemu_irq gpio_out[PCA9554_PIN_COUNT];
    uint8_t ext_state[PCA9554_PIN_COUNT];
    char *description; /* For debugging purpose only */
};

#endif
