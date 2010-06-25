/*
 * QEMU PS/2 keyboard/mouse emulation
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "hw.h"
#include "ps2.h"
#include "console.h"

/* debug PC keyboard */
//#define DEBUG_KBD

/* debug PC keyboard : only mouse */
//#define DEBUG_MOUSE

/* Keyboard Commands */
#define KBD_CMD_SET_LEDS	0xED	/* Set keyboard leds */
#define KBD_CMD_ECHO     	0xEE
#define KBD_CMD_SCANCODE	0xF0	/* Get/set scancode set */
#define KBD_CMD_GET_ID 	        0xF2	/* get keyboard ID */
#define KBD_CMD_SET_RATE	0xF3	/* Set typematic rate */
#define KBD_CMD_ENABLE		0xF4	/* Enable scanning */
#define KBD_CMD_RESET_DISABLE	0xF5	/* reset and disable scanning */
#define KBD_CMD_RESET_ENABLE   	0xF6    /* reset and enable scanning */
#define KBD_CMD_RESET		0xFF	/* Reset */

/* Keyboard Replies */
#define KBD_REPLY_POR		0xAA	/* Power on reset */
#define KBD_REPLY_ID		0xAB	/* Keyboard ID */
#define KBD_REPLY_ACK		0xFA	/* Command ACK */
#define KBD_REPLY_RESEND	0xFE	/* Command NACK, send the cmd again */

/* Mouse Commands */
#define AUX_SET_SCALE11		0xE6	/* Set 1:1 scaling */
#define AUX_SET_SCALE21		0xE7	/* Set 2:1 scaling */
#define AUX_SET_RES		0xE8	/* Set resolution */
#define AUX_GET_SCALE		0xE9	/* Get scaling factor */
#define AUX_SET_STREAM		0xEA	/* Set stream mode */
#define AUX_POLL		0xEB	/* Poll */
#define AUX_RESET_WRAP		0xEC	/* Reset wrap mode */
#define AUX_SET_WRAP		0xEE	/* Set wrap mode */
#define AUX_SET_REMOTE		0xF0	/* Set remote mode */
#define AUX_GET_TYPE		0xF2	/* Get type */
#define AUX_SET_SAMPLE		0xF3	/* Set sample rate */
#define AUX_ENABLE_DEV		0xF4	/* Enable aux device */
#define AUX_DISABLE_DEV		0xF5	/* Disable aux device */
#define AUX_SET_DEFAULT		0xF6
#define AUX_RESET		0xFF	/* Reset aux device */
#define AUX_ACK			0xFA	/* Command byte ACK. */

#define MOUSE_STATUS_REMOTE     0x40
#define MOUSE_STATUS_ENABLED    0x20
#define MOUSE_STATUS_SCALE21    0x10

#define PS2_QUEUE_SIZE 256

typedef struct {
    uint8_t data[PS2_QUEUE_SIZE];
    int rptr, wptr, count;
} PS2Queue;

typedef struct {
    PS2Queue queue;
    int32_t write_cmd;
    void (*update_irq)(void *, int);
    void *update_arg;
} PS2State;

typedef struct {
    PS2State common;
    int scan_enabled;
    /* Qemu uses translated PC scancodes internally.  To avoid multiple
       conversions we do the translation (if any) in the PS/2 emulation
       not the keyboard controller.  */
    int translate;
    int scancode_set; /* 1=XT, 2=AT, 3=PS/2 */
} PS2KbdState;

typedef struct {
    PS2State common;
    uint8_t mouse_status;
    uint8_t mouse_resolution;
    uint8_t mouse_sample_rate;
    uint8_t mouse_wrap;
    uint8_t mouse_type; /* 0 = PS2, 3 = IMPS/2, 4 = IMEX */
    uint8_t mouse_detect_state;
    int mouse_dx; /* current values, needed for 'poll' mode */
    int mouse_dy;
    int mouse_dz;
    uint8_t mouse_buttons;
} PS2MouseState;

/* Table to convert from PC scancodes to raw scancodes.  */
static const unsigned char ps2_raw_keycode[128] = {
          0,118, 22, 30, 38, 37, 46, 54, 61, 62, 70, 69, 78, 85,102, 13,
         21, 29, 36, 45, 44, 53, 60, 67, 68, 77, 84, 91, 90, 20, 28, 27,
         35, 43, 52, 51, 59, 66, 75, 76, 82, 14, 18, 93, 26, 34, 33, 42,
         50, 49, 58, 65, 73, 74, 89,124, 17, 41, 88,  5,  6,  4, 12,  3,
         11,  2, 10,  1,  9,119,126,108,117,125,123,107,115,116,121,105,
        114,122,112,113,127, 96, 97,120,  7, 15, 23, 31, 39, 47, 55, 63,
         71, 79, 86, 94,  8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 87,111,
         19, 25, 57, 81, 83, 92, 95, 98, 99,100,101,103,104,106,109,110
};

void ps2_queue(void *opaque, int b)
{
    PS2State *s = (PS2State *)opaque;
    PS2Queue *q = &s->queue;

    if (q->count >= PS2_QUEUE_SIZE)
        return;
    q->data[q->wptr] = b;
    if (++q->wptr == PS2_QUEUE_SIZE)
        q->wptr = 0;
    q->count++;
    s->update_irq(s->update_arg, 1);
}

/*
   keycode is expressed as follow:
   bit 7    - 0 key pressed, 1 = key released
   bits 6-0 - translated scancode set 2
 */
static void ps2_put_keycode(void *opaque, int keycode)
{
    PS2KbdState *s = opaque;

    /* XXX: add support for scancode sets 1 and 3 */
    if (!s->translate && keycode < 0xe0 && s->scancode_set == 2)
      {
        if (keycode & 0x80)
            ps2_queue(&s->common, 0xf0);
        keycode = ps2_raw_keycode[keycode & 0x7f];
      }
    ps2_queue(&s->common, keycode);
}

uint32_t ps2_read_data(void *opaque)
{
    PS2State *s = (PS2State *)opaque;
    PS2Queue *q;
    int val, index;

    q = &s->queue;
    if (q->count == 0) {
        /* NOTE: if no data left, we return the last keyboard one
           (needed for EMM386) */
        /* XXX: need a timer to do things correctly */
        index = q->rptr - 1;
        if (index < 0)
            index = PS2_QUEUE_SIZE - 1;
        val = q->data[index];
    } else {
        val = q->data[q->rptr];
        if (++q->rptr == PS2_QUEUE_SIZE)
            q->rptr = 0;
        q->count--;
        /* reading deasserts IRQ */
        s->update_irq(s->update_arg, 0);
        /* reassert IRQs if data left */
        s->update_irq(s->update_arg, q->count != 0);
    }
    return val;
}

static void ps2_reset_keyboard(PS2KbdState *s)
{
    s->scan_enabled = 1;
    s->scancode_set = 2;
    kbd_put_ledstate(0);
}

void ps2_write_keyboard(void *opaque, int val)
{
    PS2KbdState *s = (PS2KbdState *)opaque;

    switch(s->common.write_cmd) {
    default:
    case -1:
        switch(val) {
        case 0x00:
            ps2_queue(&s->common, KBD_REPLY_ACK);
            break;
        case 0x05:
            ps2_queue(&s->common, KBD_REPLY_RESEND);
            break;
        case KBD_CMD_GET_ID:
            ps2_queue(&s->common, KBD_REPLY_ACK);
            /* We emulate a MF2 AT keyboard here */
            ps2_queue(&s->common, KBD_REPLY_ID);
            if (s->translate)
                ps2_queue(&s->common, 0x41);
            else
                ps2_queue(&s->common, 0x83);
            break;
        case KBD_CMD_ECHO:
            ps2_queue(&s->common, KBD_CMD_ECHO);
            break;
        case KBD_CMD_ENABLE:
            s->scan_enabled = 1;
            ps2_queue(&s->common, KBD_REPLY_ACK);
            break;
        case KBD_CMD_SCANCODE:
        case KBD_CMD_SET_LEDS:
        case KBD_CMD_SET_RATE:
            s->common.write_cmd = val;
            ps2_queue(&s->common, KBD_REPLY_ACK);
            break;
        case KBD_CMD_RESET_DISABLE:
            ps2_reset_keyboard(s);
            s->scan_enabled = 0;
            ps2_queue(&s->common, KBD_REPLY_ACK);
            break;
        case KBD_CMD_RESET_ENABLE:
            ps2_reset_keyboard(s);
            s->scan_enabled = 1;
            ps2_queue(&s->common, KBD_REPLY_ACK);
            break;
        case KBD_CMD_RESET:
            ps2_reset_keyboard(s);
            ps2_queue(&s->common, KBD_REPLY_ACK);
            ps2_queue(&s->common, KBD_REPLY_POR);
            break;
        default:
            ps2_queue(&s->common, KBD_REPLY_ACK);
            break;
        }
        break;
    case KBD_CMD_SCANCODE:
        if (val == 0) {
            if (s->scancode_set == 1)
                ps2_put_keycode(s, 0x43);
            else if (s->scancode_set == 2)
                ps2_put_keycode(s, 0x41);
            else if (s->scancode_set == 3)
                ps2_put_keycode(s, 0x3f);
        } else {
            if (val >= 1 && val <= 3)
                s->scancode_set = val;
            ps2_queue(&s->common, KBD_REPLY_ACK);
        }
        s->common.write_cmd = -1;
        break;
    case KBD_CMD_SET_LEDS:
        kbd_put_ledstate(val);
        ps2_queue(&s->common, KBD_REPLY_ACK);
        s->common.write_cmd = -1;
        break;
    case KBD_CMD_SET_RATE:
        ps2_queue(&s->common, KBD_REPLY_ACK);
        s->common.write_cmd = -1;
        break;
    }
}

/* Set the scancode translation mode.
   0 = raw scancodes.
   1 = translated scancodes (used by qemu internally).  */

void ps2_keyboard_set_translation(void *opaque, int mode)
{
    PS2KbdState *s = (PS2KbdState *)opaque;
    s->translate = mode;
}

static void ps2_mouse_send_packet(PS2MouseState *s)
{
    unsigned int b;
    int dx1, dy1, dz1;

    dx1 = s->mouse_dx;
    dy1 = s->mouse_dy;
    dz1 = s->mouse_dz;
    /* XXX: increase range to 8 bits ? */
    if (dx1 > 127)
        dx1 = 127;
    else if (dx1 < -127)
        dx1 = -127;
    if (dy1 > 127)
        dy1 = 127;
    else if (dy1 < -127)
        dy1 = -127;
    b = 0x08 | ((dx1 < 0) << 4) | ((dy1 < 0) << 5) | (s->mouse_buttons & 0x07);
    ps2_queue(&s->common, b);
    ps2_queue(&s->common, dx1 & 0xff);
    ps2_queue(&s->common, dy1 & 0xff);
    /* extra byte for IMPS/2 or IMEX */
    switch(s->mouse_type) {
    default:
        break;
    case 3:
        if (dz1 > 127)
            dz1 = 127;
        else if (dz1 < -127)
                dz1 = -127;
        ps2_queue(&s->common, dz1 & 0xff);
        break;
    case 4:
        if (dz1 > 7)
            dz1 = 7;
        else if (dz1 < -7)
            dz1 = -7;
        b = (dz1 & 0x0f) | ((s->mouse_buttons & 0x18) << 1);
        ps2_queue(&s->common, b);
        break;
    }

    /* update deltas */
    s->mouse_dx -= dx1;
    s->mouse_dy -= dy1;
    s->mouse_dz -= dz1;
}

static void ps2_mouse_event(void *opaque,
                            int dx, int dy, int dz, int buttons_state)
{
    PS2MouseState *s = opaque;

    /* check if deltas are recorded when disabled */
    if (!(s->mouse_status & MOUSE_STATUS_ENABLED))
        return;

    s->mouse_dx += dx;
    s->mouse_dy -= dy;
    s->mouse_dz += dz;
    /* XXX: SDL sometimes generates nul events: we delete them */
    if (s->mouse_dx == 0 && s->mouse_dy == 0 && s->mouse_dz == 0 &&
        s->mouse_buttons == buttons_state)
	return;
    s->mouse_buttons = buttons_state;

    if (!(s->mouse_status & MOUSE_STATUS_REMOTE) &&
        (s->common.queue.count < (PS2_QUEUE_SIZE - 16))) {
        for(;;) {
            /* if not remote, send event. Multiple events are sent if
               too big deltas */
            ps2_mouse_send_packet(s);
            if (s->mouse_dx == 0 && s->mouse_dy == 0 && s->mouse_dz == 0)
                break;
        }
    }
}

void ps2_mouse_fake_event(void *opaque)
{
    ps2_mouse_event(opaque, 1, 0, 0, 0);
}

void ps2_write_mouse(void *opaque, int val)
{
    PS2MouseState *s = (PS2MouseState *)opaque;
#ifdef DEBUG_MOUSE
    printf("kbd: write mouse 0x%02x\n", val);
#endif
    switch(s->common.write_cmd) {
    default:
    case -1:
        /* mouse command */
        if (s->mouse_wrap) {
            if (val == AUX_RESET_WRAP) {
                s->mouse_wrap = 0;
                ps2_queue(&s->common, AUX_ACK);
                return;
            } else if (val != AUX_RESET) {
                ps2_queue(&s->common, val);
                return;
            }
        }
        switch(val) {
        case AUX_SET_SCALE11:
            s->mouse_status &= ~MOUSE_STATUS_SCALE21;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_SET_SCALE21:
            s->mouse_status |= MOUSE_STATUS_SCALE21;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_SET_STREAM:
            s->mouse_status &= ~MOUSE_STATUS_REMOTE;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_SET_WRAP:
            s->mouse_wrap = 1;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_SET_REMOTE:
            s->mouse_status |= MOUSE_STATUS_REMOTE;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_GET_TYPE:
            ps2_queue(&s->common, AUX_ACK);
            ps2_queue(&s->common, s->mouse_type);
            break;
        case AUX_SET_RES:
        case AUX_SET_SAMPLE:
            s->common.write_cmd = val;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_GET_SCALE:
            ps2_queue(&s->common, AUX_ACK);
            ps2_queue(&s->common, s->mouse_status);
            ps2_queue(&s->common, s->mouse_resolution);
            ps2_queue(&s->common, s->mouse_sample_rate);
            break;
        case AUX_POLL:
            ps2_queue(&s->common, AUX_ACK);
            ps2_mouse_send_packet(s);
            break;
        case AUX_ENABLE_DEV:
            s->mouse_status |= MOUSE_STATUS_ENABLED;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_DISABLE_DEV:
            s->mouse_status &= ~MOUSE_STATUS_ENABLED;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_SET_DEFAULT:
            s->mouse_sample_rate = 100;
            s->mouse_resolution = 2;
            s->mouse_status = 0;
            ps2_queue(&s->common, AUX_ACK);
            break;
        case AUX_RESET:
            s->mouse_sample_rate = 100;
            s->mouse_resolution = 2;
            s->mouse_status = 0;
            s->mouse_type = 0;
            ps2_queue(&s->common, AUX_ACK);
            ps2_queue(&s->common, 0xaa);
            ps2_queue(&s->common, s->mouse_type);
            break;
        default:
            break;
        }
        break;
    case AUX_SET_SAMPLE:
        s->mouse_sample_rate = val;
        /* detect IMPS/2 or IMEX */
        switch(s->mouse_detect_state) {
        default:
        case 0:
            if (val == 200)
                s->mouse_detect_state = 1;
            break;
        case 1:
            if (val == 100)
                s->mouse_detect_state = 2;
            else if (val == 200)
                s->mouse_detect_state = 3;
            else
                s->mouse_detect_state = 0;
            break;
        case 2:
            if (val == 80)
                s->mouse_type = 3; /* IMPS/2 */
            s->mouse_detect_state = 0;
            break;
        case 3:
            if (val == 80)
                s->mouse_type = 4; /* IMEX */
            s->mouse_detect_state = 0;
            break;
        }
        ps2_queue(&s->common, AUX_ACK);
        s->common.write_cmd = -1;
        break;
    case AUX_SET_RES:
        s->mouse_resolution = val;
        ps2_queue(&s->common, AUX_ACK);
        s->common.write_cmd = -1;
        break;
    }
}

static void ps2_common_reset(PS2State *s)
{
    PS2Queue *q;
    s->write_cmd = -1;
    q = &s->queue;
    q->rptr = 0;
    q->wptr = 0;
    q->count = 0;
    s->update_irq(s->update_arg, 0);
}

static void ps2_kbd_reset(void *opaque)
{
    PS2KbdState *s = (PS2KbdState *) opaque;

    ps2_common_reset(&s->common);
    s->scan_enabled = 0;
    s->translate = 0;
    s->scancode_set = 0;
}

static void ps2_mouse_reset(void *opaque)
{
    PS2MouseState *s = (PS2MouseState *) opaque;

    ps2_common_reset(&s->common);
    s->mouse_status = 0;
    s->mouse_resolution = 0;
    s->mouse_sample_rate = 0;
    s->mouse_wrap = 0;
    s->mouse_type = 0;
    s->mouse_detect_state = 0;
    s->mouse_dx = 0;
    s->mouse_dy = 0;
    s->mouse_dz = 0;
    s->mouse_buttons = 0;
}

static const VMStateDescription vmstate_ps2_common = {
    .name = "PS2 Common State",
    .version_id = 3,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField []) {
        VMSTATE_INT32(write_cmd, PS2State),
        VMSTATE_INT32(queue.rptr, PS2State),
        VMSTATE_INT32(queue.wptr, PS2State),
        VMSTATE_INT32(queue.count, PS2State),
        VMSTATE_BUFFER(queue.data, PS2State),
        VMSTATE_END_OF_LIST()
    }
};

static int ps2_kbd_post_load(void* opaque, int version_id)
{
    PS2KbdState *s = (PS2KbdState*)opaque;

    if (version_id == 2)
        s->scancode_set=2;
    return 0;
}

static const VMStateDescription vmstate_ps2_keyboard = {
    .name = "ps2kbd",
    .version_id = 3,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .post_load = ps2_kbd_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_STRUCT(common, PS2KbdState, 0, vmstate_ps2_common, PS2State),
        VMSTATE_INT32(scan_enabled, PS2KbdState),
        VMSTATE_INT32(translate, PS2KbdState),
        VMSTATE_INT32_V(scancode_set, PS2KbdState,3),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ps2_mouse = {
    .name = "ps2mouse",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields      = (VMStateField []) {
        VMSTATE_STRUCT(common, PS2MouseState, 0, vmstate_ps2_common, PS2State),
        VMSTATE_UINT8(mouse_status, PS2MouseState),
        VMSTATE_UINT8(mouse_resolution, PS2MouseState),
        VMSTATE_UINT8(mouse_sample_rate, PS2MouseState),
        VMSTATE_UINT8(mouse_wrap, PS2MouseState),
        VMSTATE_UINT8(mouse_type, PS2MouseState),
        VMSTATE_UINT8(mouse_detect_state, PS2MouseState),
        VMSTATE_INT32(mouse_dx, PS2MouseState),
        VMSTATE_INT32(mouse_dy, PS2MouseState),
        VMSTATE_INT32(mouse_dz, PS2MouseState),
        VMSTATE_UINT8(mouse_buttons, PS2MouseState),
        VMSTATE_END_OF_LIST()
    }
};

void *ps2_kbd_init(void (*update_irq)(void *, int), void *update_arg)
{
    PS2KbdState *s = (PS2KbdState *)qemu_mallocz(sizeof(PS2KbdState));

    s->common.update_irq = update_irq;
    s->common.update_arg = update_arg;
    s->scancode_set = 2;
    vmstate_register(NULL, 0, &vmstate_ps2_keyboard, s);
    qemu_add_kbd_event_handler(ps2_put_keycode, s);
    qemu_register_reset(ps2_kbd_reset, s);
    return s;
}

void *ps2_mouse_init(void (*update_irq)(void *, int), void *update_arg)
{
    PS2MouseState *s = (PS2MouseState *)qemu_mallocz(sizeof(PS2MouseState));

    s->common.update_irq = update_irq;
    s->common.update_arg = update_arg;
    vmstate_register(NULL, 0, &vmstate_ps2_mouse, s);
    qemu_add_mouse_event_handler(ps2_mouse_event, s, 0, "QEMU PS/2 Mouse");
    qemu_register_reset(ps2_mouse_reset, s);
    return s;
}
