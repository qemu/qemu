/*
 * QEMU keysym to keycode conversion using rdesktop keymaps
 *
 * Copyright (c) 2004 Johannes Schindelin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_KEYMAPS_H
#define QEMU_KEYMAPS_H

#include "qemu-common.h"

typedef struct {
	const char* name;
	int keysym;
} name2keysym_t;

struct key_range {
    int start;
    int end;
    struct key_range *next;
};

#define MAX_NORMAL_KEYCODE 512
#define MAX_EXTRA_COUNT 256
typedef struct {
    uint16_t keysym2keycode[MAX_NORMAL_KEYCODE];
    struct {
	int keysym;
	uint16_t keycode;
    } keysym2keycode_extra[MAX_EXTRA_COUNT];
    int extra_count;
    struct key_range *keypad_range;
    struct key_range *numlock_range;
} kbd_layout_t;

/* scancode without modifiers */
#define SCANCODE_KEYMASK 0xff
/* scancode without grey or up bit */
#define SCANCODE_KEYCODEMASK 0x7f

/* "grey" keys will usually need a 0xe0 prefix */
#define SCANCODE_GREY   0x80
#define SCANCODE_EMUL0  0xE0
/* "up" flag */
#define SCANCODE_UP     0x80

/* Additional modifiers to use if not catched another way. */
#define SCANCODE_SHIFT  0x100
#define SCANCODE_CTRL   0x200
#define SCANCODE_ALT    0x400
#define SCANCODE_ALTGR  0x800


void *init_keyboard_layout(const name2keysym_t *table, const char *language);
int keysym2scancode(void *kbd_layout, int keysym);
int keycode_is_keypad(void *kbd_layout, int keycode);
int keysym_is_numlock(void *kbd_layout, int keysym);

#endif /* QEMU_KEYMAPS_H */
