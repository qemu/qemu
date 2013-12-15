/* hw/s3c2410x.h
 *
 * Samsung s3c2440 cpu register definitions
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2.
 */

#ifndef S3C2440_H
#define S3C2440_H 1

#include "s3c24xx.h"

/* S3C2440 Physical memory areas */

/* Chip select 0 */
#define CPU_S3C2440_CS0 (0x00000000)
/* Chip select 1 */
#define CPU_S3C2440_CS1 (0x08000000)
/* Chip select 2 */
#define CPU_S3C2440_CS2 (0x10000000)
/* Chip select 3 */
#define CPU_S3C2440_CS3 (0x18000000)
/* Chip select 4 */
#define CPU_S3C2440_CS4 (0x20000000)
/* Chip select 5 */
#define CPU_S3C2440_CS5 (0x28000000)
/* Dynamic RAM */
#define CPU_S3C2440_DRAM (0x30000000)
/* SOC Integrated peripherals */
#define CPU_S3C2440_PERIPHERAL (0x40000000)

/* s3c2440 SOC initialisation */
S3CState *s3c2440_init(int sdram_size);

#endif /* S3C2440_H */
