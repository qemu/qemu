/*
 * PCA9554 I/O port registers
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PCA9554_REGS_H
#define PCA9554_REGS_H

/*
 * Bits [0:1] are used to address a specific register.
 */
#define PCA9554_INPUT       0 /* read only input register */
#define PCA9554_OUTPUT      1 /* read/write pin output state */
#define PCA9554_POLARITY    2 /* Set polarity of input register */
#define PCA9554_CONFIG      3 /* Set pins as inputs our ouputs */

#endif
