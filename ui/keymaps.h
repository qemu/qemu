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

/* scancode without modifiers */
#define SCANCODE_KEYMASK 0xff
/* scancode without grey or up bit */
#define SCANCODE_KEYCODEMASK 0x7f

/* "grey" keys will usually need a 0xe0 prefix */
#define SCANCODE_GREY   0x80
#define SCANCODE_EMUL0  0xE0
#define SCANCODE_EMUL1  0xE1
/* "up" flag */
#define SCANCODE_UP     0x80

/* Additional modifiers to use if not catched another way. */
#define SCANCODE_SHIFT  0x100
#define SCANCODE_CTRL   0x200
#define SCANCODE_ALT    0x400
#define SCANCODE_ALTGR  0x800

typedef struct kbd_layout_t kbd_layout_t;

kbd_layout_t *init_keyboard_layout(const name2keysym_t *table,
                                   const char *language, Error **errp);
int keysym2scancode(kbd_layout_t *k, int keysym,
                    bool shift, bool altgr, bool ctrl);
int keycode_is_keypad(kbd_layout_t *k, int keycode);
int keysym_is_numlock(kbd_layout_t *k, int keysym);

#endif /* QEMU_KEYMAPS_H */
