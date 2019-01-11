/*
 * QEMU PS/2 keyboard/mouse emulation
 *
 * Copyright (C) 2003 Fabrice Bellard
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

#ifndef HW_PS2_H
#define HW_PS2_H

#define PS2_MOUSE_BUTTON_LEFT   0x01
#define PS2_MOUSE_BUTTON_RIGHT  0x02
#define PS2_MOUSE_BUTTON_MIDDLE 0x04
#define PS2_MOUSE_BUTTON_SIDE   0x08
#define PS2_MOUSE_BUTTON_EXTRA  0x10

typedef struct PS2State PS2State;

/* ps2.c */
void *ps2_kbd_init(void (*update_irq)(void *, int), void *update_arg);
void *ps2_mouse_init(void (*update_irq)(void *, int), void *update_arg);
void ps2_write_mouse(void *, int val);
void ps2_write_keyboard(void *, int val);
uint32_t ps2_read_data(PS2State *s);
void ps2_queue_noirq(PS2State *s, int b);
void ps2_raise_irq(PS2State *s);
void ps2_queue(PS2State *s, int b);
void ps2_queue_2(PS2State *s, int b1, int b2);
void ps2_queue_3(PS2State *s, int b1, int b2, int b3);
void ps2_queue_4(PS2State *s, int b1, int b2, int b3, int b4);
void ps2_keyboard_set_translation(void *opaque, int mode);
void ps2_mouse_fake_event(void *opaque);

#endif /* HW_PS2_H */
