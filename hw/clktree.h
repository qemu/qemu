/*
 * Basic Clock Tree Building Blocks
 *
 * Copyright (C) 2012 Andre Beckus
 *
 * Source code roughly based on omap_clk.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CLKTREE_H
#define CLKTREE_H

#include "qemu-common.h"

#define CLKTREE_MAX_IRQ 16
#define CLKTREE_MAX_OUTPUT 16
#define CLKTREE_MAX_INPUT 16

/* Use this when calling clktree_create_clk and clktree_set_selected_input */
#define CLKTREE_NO_INPUT -1

/* Use this when calling clktree_create_clk */
#define CLKTREE_NO_MAX_FREQ UINT32_MAX

typedef struct Clk *Clk;

/* Check if the clock output is enabled. */
bool clktree_is_enabled(Clk clk);

/* Get the clock's output frequency.  This will be 0 if:
 * - No clock is selected as an input.
 * - The clock output is not enabled.
 * - The multiplier is set to 0.
 */
uint32_t clktree_get_output_freq(Clk clk);

/* Add an IRQ to receive notifications when the clock frequency is updated. */
void clktree_adduser(Clk clk, qemu_irq user);

/* Create a source clock (e.g. oscillator) with the given frequency. */
Clk clktree_create_src_clk(
                    const char *name,
                    uint32_t src_freq,
                    bool enabled);

/* Create a clock.  The vararg parameter specifies all of the source clocks
 * (use NULL to indicate the end of the list).  A warning will be generated if
 * the output frequency of the clock ever exceeds max_output_freq. */
Clk clktree_create_clk(
                    const char *name,
                    uint16_t multiplier,
                    uint16_t divisor,
                    bool enabled,
                    uint32_t max_output_freq,
                    int selected_input,
                    ...);

/* Set the multiplier and divider scales. */
void clktree_set_scale(Clk clk, uint16_t multiplier, uint16_t divisor);

/* Enable or disable the clock output. */
void clktree_set_enabled(Clk clk, bool enabled);

/* Selects the specified input clock.  selected_input should be 0 to select the
 * first input clock passed in to clktree_create_clk, 1 for the second input
 * clock, and so on.  Pass in CLKTREE_NO_INPUT to not select any input
 * (i.e. input frequency will be 0). */
void clktree_set_selected_input(Clk clk, int selected_input);

#endif
