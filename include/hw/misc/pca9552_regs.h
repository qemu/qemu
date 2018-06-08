/*
 * PCA9552 I2C LED blinker registers
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#ifndef PCA9552_REGS_H
#define PCA9552_REGS_H

/*
 * Bits [0:3] are used to address a specific register.
 */
#define PCA9552_INPUT0   0 /* read only input register 0 */
#define PCA9552_INPUT1   1 /* read only input register 1  */
#define PCA9552_PSC0     2 /* read/write frequency prescaler 0 */
#define PCA9552_PWM0     3 /* read/write PWM register 0 */
#define PCA9552_PSC1     4 /* read/write frequency prescaler 1 */
#define PCA9552_PWM1     5 /* read/write PWM register 1 */
#define PCA9552_LS0      6 /* read/write LED0 to LED3 selector */
#define PCA9552_LS1      7 /* read/write LED4 to LED7 selector */
#define PCA9552_LS2      8 /* read/write LED8 to LED11 selector */
#define PCA9552_LS3      9 /* read/write LED12 to LED15 selector */

/*
 * Bit [4] is used to activate the Auto-Increment option of the
 * register address
 */
#define PCA9552_AUTOINC  (1 << 4)

#endif
