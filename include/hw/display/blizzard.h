/*
 * Epson S1D13744/S1D13745 (Blizzard/Hailstorm/Tornado) LCD/TV controller.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_DISPLAY_BLIZZARD_H
#define HW_DISPLAY_BLIZZARD_H

#include "hw/irq.h"

void *s1d13745_init(qemu_irq gpio_int);
void s1d13745_write(void *opaque, int dc, uint16_t value);
void s1d13745_write_block(void *opaque, int dc,
                          void *buf, size_t len, int pitch);
uint16_t s1d13745_read(void *opaque, int dc);

#endif
