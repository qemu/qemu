/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_INPUT_GAMEPAD_H
#define HW_INPUT_GAMEPAD_H

#include "hw/irq.h"

/* stellaris_input.c */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode);

#endif
