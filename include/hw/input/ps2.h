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

#include "hw/sysbus.h"

#define PS2_MOUSE_BUTTON_LEFT   0x01
#define PS2_MOUSE_BUTTON_RIGHT  0x02
#define PS2_MOUSE_BUTTON_MIDDLE 0x04
#define PS2_MOUSE_BUTTON_SIDE   0x08
#define PS2_MOUSE_BUTTON_EXTRA  0x10

struct PS2DeviceClass {
    SysBusDeviceClass parent_class;

    DeviceReset parent_reset;
};

/*
 * PS/2 buffer size. Keep 256 bytes for compatibility with
 * older QEMU versions.
 */
#define PS2_BUFFER_SIZE     256

typedef struct {
    uint8_t data[PS2_BUFFER_SIZE];
    int rptr, wptr, cwptr, count;
} PS2Queue;

/* Output IRQ */
#define PS2_DEVICE_IRQ      0

struct PS2State {
    SysBusDevice parent_obj;

    PS2Queue queue;
    int32_t write_cmd;
    qemu_irq irq;
};

#define TYPE_PS2_DEVICE "ps2-device"
OBJECT_DECLARE_TYPE(PS2State, PS2DeviceClass, PS2_DEVICE)

struct PS2KbdState {
    PS2State parent_obj;

    int scan_enabled;
    int translate;
    int scancode_set; /* 1=XT, 2=AT, 3=PS/2 */
    int ledstate;
    bool need_high_bit;
    unsigned int modifiers; /* bitmask of MOD_* constants above */
};

#define TYPE_PS2_KBD_DEVICE "ps2-kbd"
OBJECT_DECLARE_SIMPLE_TYPE(PS2KbdState, PS2_KBD_DEVICE)

struct PS2MouseState {
    PS2State parent_obj;

    uint8_t mouse_status;
    uint8_t mouse_resolution;
    uint8_t mouse_sample_rate;
    uint8_t mouse_wrap;
    uint8_t mouse_type; /* 0 = PS2, 3 = IMPS/2, 4 = IMEX */
    uint8_t mouse_detect_state;
    int mouse_dx; /* current values, needed for 'poll' mode */
    int mouse_dy;
    int mouse_dz;
    int mouse_dw;
    uint8_t mouse_buttons;
};

#define TYPE_PS2_MOUSE_DEVICE "ps2-mouse"
OBJECT_DECLARE_SIMPLE_TYPE(PS2MouseState, PS2_MOUSE_DEVICE)

/* ps2.c */
void ps2_write_mouse(PS2MouseState *s, int val);
void ps2_write_keyboard(PS2KbdState *s, int val);
uint32_t ps2_read_data(PS2State *s);
void ps2_queue_noirq(PS2State *s, int b);
void ps2_queue(PS2State *s, int b);
void ps2_queue_2(PS2State *s, int b1, int b2);
void ps2_queue_3(PS2State *s, int b1, int b2, int b3);
void ps2_queue_4(PS2State *s, int b1, int b2, int b3, int b4);
void ps2_keyboard_set_translation(PS2KbdState *s, int mode);
void ps2_mouse_fake_event(PS2MouseState *s);
int ps2_queue_empty(PS2State *s);

#endif /* HW_PS2_H */
