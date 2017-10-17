/*
 * Misc PKUnity SoC declarations
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_HW_PUV3_H
#define QEMU_HW_PUV3_H

#define PUV3_REGS_OFFSET        (0x1000) /* 4K is reasonable */

/* Hardware interrupts */
#define PUV3_IRQS_NR            (32)

#define PUV3_IRQS_GPIOLOW0      (0)
#define PUV3_IRQS_GPIOLOW1      (1)
#define PUV3_IRQS_GPIOLOW2      (2)
#define PUV3_IRQS_GPIOLOW3      (3)
#define PUV3_IRQS_GPIOLOW4      (4)
#define PUV3_IRQS_GPIOLOW5      (5)
#define PUV3_IRQS_GPIOLOW6      (6)
#define PUV3_IRQS_GPIOLOW7      (7)
#define PUV3_IRQS_GPIOHIGH      (8)
#define PUV3_IRQS_PS2_KBD       (22)
#define PUV3_IRQS_PS2_AUX       (23)
#define PUV3_IRQS_OST0          (26)

/* All puv3_*.c use DPRINTF for debug. */
#ifdef DEBUG_PUV3
#define DPRINTF(fmt, ...) printf("%s: " fmt , __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#endif /* QEMU_HW_PUV3_H */
