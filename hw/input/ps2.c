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
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/hw.h"
#include "hw/input/ps2.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"

#include "trace.h"

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

#define PS2_QUEUE_SIZE 16  /* Buffer size required by PS/2 protocol */

typedef struct {
    /* Keep the data array 256 bytes long, which compatibility
     with older qemu versions. */
    uint8_t data[256];
    int rptr, wptr, count;
} PS2Queue;

struct PS2State {
    PS2Queue queue;
    int32_t write_cmd;
    void (*update_irq)(void *, int);
    void *update_arg;
};

typedef struct {
    PS2State common;
    int scan_enabled;
    int translate;
    int scancode_set; /* 1=XT, 2=AT, 3=PS/2 */
    int ledstate;
    bool need_high_bit;
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

/* Table to convert from QEMU codes to scancodes.  */
static const uint16_t qcode_to_keycode_set1[Q_KEY_CODE__MAX] = {
    [0 ... Q_KEY_CODE__MAX - 1] = 0,

    [Q_KEY_CODE_A] = 0x1e,
    [Q_KEY_CODE_B] = 0x30,
    [Q_KEY_CODE_C] = 0x2e,
    [Q_KEY_CODE_D] = 0x20,
    [Q_KEY_CODE_E] = 0x12,
    [Q_KEY_CODE_F] = 0x21,
    [Q_KEY_CODE_G] = 0x22,
    [Q_KEY_CODE_H] = 0x23,
    [Q_KEY_CODE_I] = 0x17,
    [Q_KEY_CODE_J] = 0x24,
    [Q_KEY_CODE_K] = 0x25,
    [Q_KEY_CODE_L] = 0x26,
    [Q_KEY_CODE_M] = 0x32,
    [Q_KEY_CODE_N] = 0x31,
    [Q_KEY_CODE_O] = 0x18,
    [Q_KEY_CODE_P] = 0x19,
    [Q_KEY_CODE_Q] = 0x10,
    [Q_KEY_CODE_R] = 0x13,
    [Q_KEY_CODE_S] = 0x1f,
    [Q_KEY_CODE_T] = 0x14,
    [Q_KEY_CODE_U] = 0x16,
    [Q_KEY_CODE_V] = 0x2f,
    [Q_KEY_CODE_W] = 0x11,
    [Q_KEY_CODE_X] = 0x2d,
    [Q_KEY_CODE_Y] = 0x15,
    [Q_KEY_CODE_Z] = 0x2c,
    [Q_KEY_CODE_0] = 0x0b,
    [Q_KEY_CODE_1] = 0x02,
    [Q_KEY_CODE_2] = 0x03,
    [Q_KEY_CODE_3] = 0x04,
    [Q_KEY_CODE_4] = 0x05,
    [Q_KEY_CODE_5] = 0x06,
    [Q_KEY_CODE_6] = 0x07,
    [Q_KEY_CODE_7] = 0x08,
    [Q_KEY_CODE_8] = 0x09,
    [Q_KEY_CODE_9] = 0x0a,
    [Q_KEY_CODE_GRAVE_ACCENT] = 0x29,
    [Q_KEY_CODE_MINUS] = 0x0c,
    [Q_KEY_CODE_EQUAL] = 0x0d,
    [Q_KEY_CODE_BACKSLASH] = 0x2b,
    [Q_KEY_CODE_BACKSPACE] = 0x0e,
    [Q_KEY_CODE_SPC] = 0x39,
    [Q_KEY_CODE_TAB] = 0x0f,
    [Q_KEY_CODE_CAPS_LOCK] = 0x3a,
    [Q_KEY_CODE_SHIFT] = 0x2a,
    [Q_KEY_CODE_CTRL] = 0x1d,
    [Q_KEY_CODE_META_L] = 0xe05b,
    [Q_KEY_CODE_ALT] = 0x38,
    [Q_KEY_CODE_SHIFT_R] = 0x36,
    [Q_KEY_CODE_CTRL_R] = 0xe01d,
    [Q_KEY_CODE_META_R] = 0xe05c,
    [Q_KEY_CODE_ALT_R] = 0xe038,
    [Q_KEY_CODE_MENU] = 0xe05d,
    [Q_KEY_CODE_RET] = 0x1c,
    [Q_KEY_CODE_ESC] = 0x01,
    [Q_KEY_CODE_F1] = 0x3b,
    [Q_KEY_CODE_F2] = 0x3c,
    [Q_KEY_CODE_F3] = 0x3d,
    [Q_KEY_CODE_F4] = 0x3e,
    [Q_KEY_CODE_F5] = 0x3f,
    [Q_KEY_CODE_F6] = 0x40,
    [Q_KEY_CODE_F7] = 0x41,
    [Q_KEY_CODE_F8] = 0x42,
    [Q_KEY_CODE_F9] = 0x43,
    [Q_KEY_CODE_F10] = 0x44,
    [Q_KEY_CODE_F11] = 0x57,
    [Q_KEY_CODE_F12] = 0x58,
    /* special handling for Q_KEY_CODE_PRINT */
    [Q_KEY_CODE_SCROLL_LOCK] = 0x46,
    /* special handling for Q_KEY_CODE_PAUSE */
    [Q_KEY_CODE_BRACKET_LEFT] = 0x1a,
    [Q_KEY_CODE_INSERT] = 0xe052,
    [Q_KEY_CODE_HOME] = 0xe047,
    [Q_KEY_CODE_PGUP] = 0xe049,
    [Q_KEY_CODE_DELETE] = 0xe053,
    [Q_KEY_CODE_END] = 0xe04f,
    [Q_KEY_CODE_PGDN] = 0xe051,
    [Q_KEY_CODE_UP] = 0xe048,
    [Q_KEY_CODE_LEFT] = 0xe04b,
    [Q_KEY_CODE_DOWN] = 0xe050,
    [Q_KEY_CODE_RIGHT] = 0xe04d,
    [Q_KEY_CODE_NUM_LOCK] = 0x45,
    [Q_KEY_CODE_KP_DIVIDE] = 0xe035,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x37,
    [Q_KEY_CODE_KP_SUBTRACT] = 0x4a,
    [Q_KEY_CODE_KP_ADD] = 0x4e,
    [Q_KEY_CODE_KP_ENTER] = 0xe01c,
    [Q_KEY_CODE_KP_DECIMAL] = 0x53,
    [Q_KEY_CODE_KP_0] = 0x52,
    [Q_KEY_CODE_KP_1] = 0x4f,
    [Q_KEY_CODE_KP_2] = 0x50,
    [Q_KEY_CODE_KP_3] = 0x51,
    [Q_KEY_CODE_KP_4] = 0x4b,
    [Q_KEY_CODE_KP_5] = 0x4c,
    [Q_KEY_CODE_KP_6] = 0x4d,
    [Q_KEY_CODE_KP_7] = 0x47,
    [Q_KEY_CODE_KP_8] = 0x48,
    [Q_KEY_CODE_KP_9] = 0x49,
    [Q_KEY_CODE_BRACKET_RIGHT] = 0x1b,
    [Q_KEY_CODE_SEMICOLON] = 0x27,
    [Q_KEY_CODE_APOSTROPHE] = 0x28,
    [Q_KEY_CODE_COMMA] = 0x33,
    [Q_KEY_CODE_DOT] = 0x34,
    [Q_KEY_CODE_SLASH] = 0x35,

    [Q_KEY_CODE_POWER] = 0x0e5e,
    [Q_KEY_CODE_SLEEP] = 0x0e5f,
    [Q_KEY_CODE_WAKE] = 0x0e63,

    [Q_KEY_CODE_AUDIONEXT] = 0xe019,
    [Q_KEY_CODE_AUDIOPREV] = 0xe010,
    [Q_KEY_CODE_AUDIOSTOP] = 0xe024,
    [Q_KEY_CODE_AUDIOPLAY] = 0xe022,
    [Q_KEY_CODE_AUDIOMUTE] = 0xe020,
    [Q_KEY_CODE_VOLUMEUP] = 0xe030,
    [Q_KEY_CODE_VOLUMEDOWN] = 0xe02e,
    [Q_KEY_CODE_MEDIASELECT] = 0xe06d,
    [Q_KEY_CODE_MAIL] = 0xe06c,
    [Q_KEY_CODE_CALCULATOR] = 0xe021,
    [Q_KEY_CODE_COMPUTER] = 0xe06b,
    [Q_KEY_CODE_FIND] = 0xe065,
    [Q_KEY_CODE_AC_HOME] = 0xe032,
    [Q_KEY_CODE_AC_BACK] = 0xe06a,
    [Q_KEY_CODE_AC_FORWARD] = 0xe069,
    [Q_KEY_CODE_STOP] = 0xe068,
    [Q_KEY_CODE_AC_REFRESH] = 0xe067,
    [Q_KEY_CODE_AC_BOOKMARKS] = 0xe066,

    [Q_KEY_CODE_ASTERISK] = 0x37,
    [Q_KEY_CODE_LESS] = 0x56,
    [Q_KEY_CODE_RO] = 0x73,
    [Q_KEY_CODE_HIRAGANA] = 0x70,
    [Q_KEY_CODE_HENKAN] = 0x79,
    [Q_KEY_CODE_YEN] = 0x7d,
    [Q_KEY_CODE_KP_COMMA] = 0x7e,
};

static const uint16_t qcode_to_keycode_set2[Q_KEY_CODE__MAX] = {
    [0 ... Q_KEY_CODE__MAX - 1] = 0,

    [Q_KEY_CODE_A] = 0x1c,
    [Q_KEY_CODE_B] = 0x32,
    [Q_KEY_CODE_C] = 0x21,
    [Q_KEY_CODE_D] = 0x23,
    [Q_KEY_CODE_E] = 0x24,
    [Q_KEY_CODE_F] = 0x2b,
    [Q_KEY_CODE_G] = 0x34,
    [Q_KEY_CODE_H] = 0x33,
    [Q_KEY_CODE_I] = 0x43,
    [Q_KEY_CODE_J] = 0x3b,
    [Q_KEY_CODE_K] = 0x42,
    [Q_KEY_CODE_L] = 0x4b,
    [Q_KEY_CODE_M] = 0x3a,
    [Q_KEY_CODE_N] = 0x31,
    [Q_KEY_CODE_O] = 0x44,
    [Q_KEY_CODE_P] = 0x4d,
    [Q_KEY_CODE_Q] = 0x15,
    [Q_KEY_CODE_R] = 0x2d,
    [Q_KEY_CODE_S] = 0x1b,
    [Q_KEY_CODE_T] = 0x2c,
    [Q_KEY_CODE_U] = 0x3c,
    [Q_KEY_CODE_V] = 0x2a,
    [Q_KEY_CODE_W] = 0x1d,
    [Q_KEY_CODE_X] = 0x22,
    [Q_KEY_CODE_Y] = 0x35,
    [Q_KEY_CODE_Z] = 0x1a,
    [Q_KEY_CODE_0] = 0x45,
    [Q_KEY_CODE_1] = 0x16,
    [Q_KEY_CODE_2] = 0x1e,
    [Q_KEY_CODE_3] = 0x26,
    [Q_KEY_CODE_4] = 0x25,
    [Q_KEY_CODE_5] = 0x2e,
    [Q_KEY_CODE_6] = 0x36,
    [Q_KEY_CODE_7] = 0x3d,
    [Q_KEY_CODE_8] = 0x3e,
    [Q_KEY_CODE_9] = 0x46,
    [Q_KEY_CODE_GRAVE_ACCENT] = 0x0e,
    [Q_KEY_CODE_MINUS] = 0x4e,
    [Q_KEY_CODE_EQUAL] = 0x55,
    [Q_KEY_CODE_BACKSLASH] = 0x5d,
    [Q_KEY_CODE_BACKSPACE] = 0x66,
    [Q_KEY_CODE_SPC] = 0x29,
    [Q_KEY_CODE_TAB] = 0x0d,
    [Q_KEY_CODE_CAPS_LOCK] = 0x58,
    [Q_KEY_CODE_SHIFT] = 0x12,
    [Q_KEY_CODE_CTRL] = 0x14,
    [Q_KEY_CODE_META_L] = 0xe01f,
    [Q_KEY_CODE_ALT] = 0x11,
    [Q_KEY_CODE_SHIFT_R] = 0x59,
    [Q_KEY_CODE_CTRL_R] = 0xe014,
    [Q_KEY_CODE_META_R] = 0xe027,
    [Q_KEY_CODE_ALT_R] = 0xe011,
    [Q_KEY_CODE_MENU] = 0xe02f,
    [Q_KEY_CODE_RET] = 0x5a,
    [Q_KEY_CODE_ESC] = 0x76,
    [Q_KEY_CODE_F1] = 0x05,
    [Q_KEY_CODE_F2] = 0x06,
    [Q_KEY_CODE_F3] = 0x04,
    [Q_KEY_CODE_F4] = 0x0c,
    [Q_KEY_CODE_F5] = 0x03,
    [Q_KEY_CODE_F6] = 0x0b,
    [Q_KEY_CODE_F7] = 0x83,
    [Q_KEY_CODE_F8] = 0x0a,
    [Q_KEY_CODE_F9] = 0x01,
    [Q_KEY_CODE_F10] = 0x09,
    [Q_KEY_CODE_F11] = 0x78,
    [Q_KEY_CODE_F12] = 0x07,
    /* special handling for Q_KEY_CODE_PRINT */
    [Q_KEY_CODE_SCROLL_LOCK] = 0x7e,
    /* special handling for Q_KEY_CODE_PAUSE */
    [Q_KEY_CODE_BRACKET_LEFT] = 0x54,
    [Q_KEY_CODE_INSERT] = 0xe070,
    [Q_KEY_CODE_HOME] = 0xe06c,
    [Q_KEY_CODE_PGUP] = 0xe07d,
    [Q_KEY_CODE_DELETE] = 0xe071,
    [Q_KEY_CODE_END] = 0xe069,
    [Q_KEY_CODE_PGDN] = 0xe07a,
    [Q_KEY_CODE_UP] = 0xe075,
    [Q_KEY_CODE_LEFT] = 0xe06b,
    [Q_KEY_CODE_DOWN] = 0xe072,
    [Q_KEY_CODE_RIGHT] = 0xe074,
    [Q_KEY_CODE_NUM_LOCK] = 0x77,
    [Q_KEY_CODE_KP_DIVIDE] = 0xe04a,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x7c,
    [Q_KEY_CODE_KP_SUBTRACT] = 0x7b,
    [Q_KEY_CODE_KP_ADD] = 0x79,
    [Q_KEY_CODE_KP_ENTER] = 0xe05a,
    [Q_KEY_CODE_KP_DECIMAL] = 0x71,
    [Q_KEY_CODE_KP_0] = 0x70,
    [Q_KEY_CODE_KP_1] = 0x69,
    [Q_KEY_CODE_KP_2] = 0x72,
    [Q_KEY_CODE_KP_3] = 0x7a,
    [Q_KEY_CODE_KP_4] = 0x6b,
    [Q_KEY_CODE_KP_5] = 0x73,
    [Q_KEY_CODE_KP_6] = 0x74,
    [Q_KEY_CODE_KP_7] = 0x6c,
    [Q_KEY_CODE_KP_8] = 0x75,
    [Q_KEY_CODE_KP_9] = 0x7d,
    [Q_KEY_CODE_BRACKET_RIGHT] = 0x5b,
    [Q_KEY_CODE_SEMICOLON] = 0x4c,
    [Q_KEY_CODE_APOSTROPHE] = 0x52,
    [Q_KEY_CODE_COMMA] = 0x41,
    [Q_KEY_CODE_DOT] = 0x49,
    [Q_KEY_CODE_SLASH] = 0x4a,

    [Q_KEY_CODE_POWER] = 0x0e37,
    [Q_KEY_CODE_SLEEP] = 0x0e3f,
    [Q_KEY_CODE_WAKE] = 0x0e5e,

    [Q_KEY_CODE_AUDIONEXT] = 0xe04d,
    [Q_KEY_CODE_AUDIOPREV] = 0xe015,
    [Q_KEY_CODE_AUDIOSTOP] = 0xe03b,
    [Q_KEY_CODE_AUDIOPLAY] = 0xe034,
    [Q_KEY_CODE_AUDIOMUTE] = 0xe023,
    [Q_KEY_CODE_VOLUMEUP] = 0xe032,
    [Q_KEY_CODE_VOLUMEDOWN] = 0xe021,
    [Q_KEY_CODE_MEDIASELECT] = 0xe050,
    [Q_KEY_CODE_MAIL] = 0xe048,
    [Q_KEY_CODE_CALCULATOR] = 0xe02b,
    [Q_KEY_CODE_COMPUTER] = 0xe040,
    [Q_KEY_CODE_FIND] = 0xe010,
    [Q_KEY_CODE_AC_HOME] = 0xe03a,
    [Q_KEY_CODE_AC_BACK] = 0xe038,
    [Q_KEY_CODE_AC_FORWARD] = 0xe030,
    [Q_KEY_CODE_STOP] = 0xe028,
    [Q_KEY_CODE_AC_REFRESH] = 0xe020,
    [Q_KEY_CODE_AC_BOOKMARKS] = 0xe018,

    [Q_KEY_CODE_ASTERISK] = 0x7c,
    [Q_KEY_CODE_LESS] = 0x61,
    [Q_KEY_CODE_SYSRQ] = 0x7f,
    [Q_KEY_CODE_RO] = 0x51,
    [Q_KEY_CODE_HIRAGANA] = 0x13,
    [Q_KEY_CODE_HENKAN] = 0x64,
    [Q_KEY_CODE_YEN] = 0x6a,
    [Q_KEY_CODE_KP_COMMA] = 0x6d,
};

static const uint16_t qcode_to_keycode_set3[Q_KEY_CODE__MAX] = {
    [0 ... Q_KEY_CODE__MAX - 1] = 0,

    [Q_KEY_CODE_A] = 0x1c,
    [Q_KEY_CODE_B] = 0x32,
    [Q_KEY_CODE_C] = 0x21,
    [Q_KEY_CODE_D] = 0x23,
    [Q_KEY_CODE_E] = 0x24,
    [Q_KEY_CODE_F] = 0x2b,
    [Q_KEY_CODE_G] = 0x34,
    [Q_KEY_CODE_H] = 0x33,
    [Q_KEY_CODE_I] = 0x43,
    [Q_KEY_CODE_J] = 0x3b,
    [Q_KEY_CODE_K] = 0x42,
    [Q_KEY_CODE_L] = 0x4b,
    [Q_KEY_CODE_M] = 0x3a,
    [Q_KEY_CODE_N] = 0x31,
    [Q_KEY_CODE_O] = 0x44,
    [Q_KEY_CODE_P] = 0x4d,
    [Q_KEY_CODE_Q] = 0x15,
    [Q_KEY_CODE_R] = 0x2d,
    [Q_KEY_CODE_S] = 0x1b,
    [Q_KEY_CODE_T] = 0x2c,
    [Q_KEY_CODE_U] = 0x3c,
    [Q_KEY_CODE_V] = 0x2a,
    [Q_KEY_CODE_W] = 0x1d,
    [Q_KEY_CODE_X] = 0x22,
    [Q_KEY_CODE_Y] = 0x35,
    [Q_KEY_CODE_Z] = 0x1a,
    [Q_KEY_CODE_0] = 0x45,
    [Q_KEY_CODE_1] = 0x16,
    [Q_KEY_CODE_2] = 0x1e,
    [Q_KEY_CODE_3] = 0x26,
    [Q_KEY_CODE_4] = 0x25,
    [Q_KEY_CODE_5] = 0x2e,
    [Q_KEY_CODE_6] = 0x36,
    [Q_KEY_CODE_7] = 0x3d,
    [Q_KEY_CODE_8] = 0x3e,
    [Q_KEY_CODE_9] = 0x46,
    [Q_KEY_CODE_GRAVE_ACCENT] = 0x0e,
    [Q_KEY_CODE_MINUS] = 0x4e,
    [Q_KEY_CODE_EQUAL] = 0x55,
    [Q_KEY_CODE_BACKSLASH] = 0x5c,
    [Q_KEY_CODE_BACKSPACE] = 0x66,
    [Q_KEY_CODE_SPC] = 0x29,
    [Q_KEY_CODE_TAB] = 0x0d,
    [Q_KEY_CODE_CAPS_LOCK] = 0x14,
    [Q_KEY_CODE_SHIFT] = 0x12,
    [Q_KEY_CODE_CTRL] = 0x11,
    [Q_KEY_CODE_META_L] = 0x8b,
    [Q_KEY_CODE_ALT] = 0x19,
    [Q_KEY_CODE_SHIFT_R] = 0x59,
    [Q_KEY_CODE_CTRL_R] = 0x58,
    [Q_KEY_CODE_META_R] = 0x8c,
    [Q_KEY_CODE_ALT_R] = 0x39,
    [Q_KEY_CODE_MENU] = 0x8d,
    [Q_KEY_CODE_RET] = 0x5a,
    [Q_KEY_CODE_ESC] = 0x08,
    [Q_KEY_CODE_F1] = 0x07,
    [Q_KEY_CODE_F2] = 0x0f,
    [Q_KEY_CODE_F3] = 0x17,
    [Q_KEY_CODE_F4] = 0x1f,
    [Q_KEY_CODE_F5] = 0x27,
    [Q_KEY_CODE_F6] = 0x2f,
    [Q_KEY_CODE_F7] = 0x37,
    [Q_KEY_CODE_F8] = 0x3f,
    [Q_KEY_CODE_F9] = 0x47,
    [Q_KEY_CODE_F10] = 0x4f,
    [Q_KEY_CODE_F11] = 0x56,
    [Q_KEY_CODE_F12] = 0x5e,
    [Q_KEY_CODE_PRINT] = 0x57,
    [Q_KEY_CODE_SCROLL_LOCK] = 0x5f,
    [Q_KEY_CODE_PAUSE] = 0x62,
    [Q_KEY_CODE_BRACKET_LEFT] = 0x54,
    [Q_KEY_CODE_INSERT] = 0x67,
    [Q_KEY_CODE_HOME] = 0x6e,
    [Q_KEY_CODE_PGUP] = 0x6f,
    [Q_KEY_CODE_DELETE] = 0x64,
    [Q_KEY_CODE_END] = 0x65,
    [Q_KEY_CODE_PGDN] = 0x6d,
    [Q_KEY_CODE_UP] = 0x63,
    [Q_KEY_CODE_LEFT] = 0x61,
    [Q_KEY_CODE_DOWN] = 0x60,
    [Q_KEY_CODE_RIGHT] = 0x6a,
    [Q_KEY_CODE_NUM_LOCK] = 0x76,
    [Q_KEY_CODE_KP_DIVIDE] = 0x4a,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x7e,
    [Q_KEY_CODE_KP_SUBTRACT] = 0x4e,
    [Q_KEY_CODE_KP_ADD] = 0x7c,
    [Q_KEY_CODE_KP_ENTER] = 0x79,
    [Q_KEY_CODE_KP_DECIMAL] = 0x71,
    [Q_KEY_CODE_KP_0] = 0x70,
    [Q_KEY_CODE_KP_1] = 0x69,
    [Q_KEY_CODE_KP_2] = 0x72,
    [Q_KEY_CODE_KP_3] = 0x7a,
    [Q_KEY_CODE_KP_4] = 0x6b,
    [Q_KEY_CODE_KP_5] = 0x73,
    [Q_KEY_CODE_KP_6] = 0x74,
    [Q_KEY_CODE_KP_7] = 0x6c,
    [Q_KEY_CODE_KP_8] = 0x75,
    [Q_KEY_CODE_KP_9] = 0x7d,
    [Q_KEY_CODE_BRACKET_RIGHT] = 0x5b,
    [Q_KEY_CODE_SEMICOLON] = 0x4c,
    [Q_KEY_CODE_APOSTROPHE] = 0x52,
    [Q_KEY_CODE_COMMA] = 0x41,
    [Q_KEY_CODE_DOT] = 0x49,
    [Q_KEY_CODE_SLASH] = 0x4a,

    [Q_KEY_CODE_HIRAGANA] = 0x87,
    [Q_KEY_CODE_HENKAN] = 0x86,
    [Q_KEY_CODE_YEN] = 0x5d,
};

static uint8_t translate_table[256] = {
    0xff, 0x43, 0x41, 0x3f, 0x3d, 0x3b, 0x3c, 0x58,
    0x64, 0x44, 0x42, 0x40, 0x3e, 0x0f, 0x29, 0x59,
    0x65, 0x38, 0x2a, 0x70, 0x1d, 0x10, 0x02, 0x5a,
    0x66, 0x71, 0x2c, 0x1f, 0x1e, 0x11, 0x03, 0x5b,
    0x67, 0x2e, 0x2d, 0x20, 0x12, 0x05, 0x04, 0x5c,
    0x68, 0x39, 0x2f, 0x21, 0x14, 0x13, 0x06, 0x5d,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5e,
    0x6a, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5f,
    0x6b, 0x33, 0x25, 0x17, 0x18, 0x0b, 0x0a, 0x60,
    0x6c, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0c, 0x61,
    0x6d, 0x73, 0x28, 0x74, 0x1a, 0x0d, 0x62, 0x6e,
    0x3a, 0x36, 0x1c, 0x1b, 0x75, 0x2b, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7a, 0x0e, 0x7b,
    0x7c, 0x4f, 0x7d, 0x4b, 0x47, 0x7e, 0x7f, 0x6f,
    0x52, 0x53, 0x50, 0x4c, 0x4d, 0x48, 0x01, 0x45,
    0x57, 0x4e, 0x51, 0x4a, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

static void ps2_reset_queue(PS2State *s)
{
    PS2Queue *q = &s->queue;

    q->rptr = 0;
    q->wptr = 0;
    q->count = 0;
}

void ps2_queue(PS2State *s, int b)
{
    PS2Queue *q = &s->queue;

    if (q->count >= PS2_QUEUE_SIZE - 1)
        return;
    q->data[q->wptr] = b;
    if (++q->wptr == PS2_QUEUE_SIZE)
        q->wptr = 0;
    q->count++;
    s->update_irq(s->update_arg, 1);
}

/* keycode is the untranslated scancode in the current scancode set. */
static void ps2_put_keycode(void *opaque, int keycode)
{
    PS2KbdState *s = opaque;

    trace_ps2_put_keycode(opaque, keycode);
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER);

    if (s->translate) {
        if (keycode == 0xf0) {
            s->need_high_bit = true;
        } else if (s->need_high_bit) {
            ps2_queue(&s->common, translate_table[keycode] | 0x80);
            s->need_high_bit = false;
        } else {
            ps2_queue(&s->common, translate_table[keycode]);
        }
    } else {
        ps2_queue(&s->common, keycode);
    }
}

static void ps2_keyboard_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    PS2KbdState *s = (PS2KbdState *)dev;
    InputKeyEvent *key = evt->u.key.data;
    int qcode;
    uint16_t keycode;

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER);
    assert(evt->type == INPUT_EVENT_KIND_KEY);
    qcode = qemu_input_key_value_to_qcode(key->key);

    if (s->scancode_set == 1) {
        if (qcode == Q_KEY_CODE_PAUSE) {
            if (key->down) {
                ps2_put_keycode(s, 0xe1);
                ps2_put_keycode(s, 0x1d);
                ps2_put_keycode(s, 0x45);
                ps2_put_keycode(s, 0x91);
                ps2_put_keycode(s, 0x9d);
                ps2_put_keycode(s, 0xc5);
            }
        } else if (qcode == Q_KEY_CODE_PRINT) {
            if (key->down) {
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0x2a);
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0x37);
            } else {
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0xb7);
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0xaa);
            }
        } else {
            keycode = qcode_to_keycode_set1[qcode];
            if (keycode) {
                if (keycode & 0xff00) {
                    ps2_put_keycode(s, keycode >> 8);
                }
                if (!key->down) {
                    keycode |= 0x80;
                }
                ps2_put_keycode(s, keycode & 0xff);
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "ps2: ignoring key with qcode %d\n", qcode);
            }
        }
    } else if (s->scancode_set == 2) {
        if (qcode == Q_KEY_CODE_PAUSE) {
            if (key->down) {
                ps2_put_keycode(s, 0xe1);
                ps2_put_keycode(s, 0x14);
                ps2_put_keycode(s, 0x77);
                ps2_put_keycode(s, 0xe1);
                ps2_put_keycode(s, 0xf0);
                ps2_put_keycode(s, 0x14);
                ps2_put_keycode(s, 0xf0);
                ps2_put_keycode(s, 0x77);
            }
        } else if (qcode == Q_KEY_CODE_PRINT) {
            if (key->down) {
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0x12);
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0x7c);
            } else {
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0xf0);
                ps2_put_keycode(s, 0x7c);
                ps2_put_keycode(s, 0xe0);
                ps2_put_keycode(s, 0xf0);
                ps2_put_keycode(s, 0x12);
            }
        } else {
            keycode = qcode_to_keycode_set2[qcode];
            if (keycode) {
                if (keycode & 0xff00) {
                    ps2_put_keycode(s, keycode >> 8);
                }
                if (!key->down) {
                    ps2_put_keycode(s, 0xf0);
                }
                ps2_put_keycode(s, keycode & 0xff);
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "ps2: ignoring key with qcode %d\n", qcode);
            }
        }
    } else if (s->scancode_set == 3) {
        keycode = qcode_to_keycode_set3[qcode];
        if (keycode) {
            /* FIXME: break code should be configured on a key by key basis */
            if (!key->down) {
                ps2_put_keycode(s, 0xf0);
            }
            ps2_put_keycode(s, keycode);
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "ps2: ignoring key with qcode %d\n", qcode);
        }
    }
}

uint32_t ps2_read_data(PS2State *s)
{
    PS2Queue *q;
    int val, index;

    trace_ps2_read_data(s);
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

static void ps2_set_ledstate(PS2KbdState *s, int ledstate)
{
    trace_ps2_set_ledstate(s, ledstate);
    s->ledstate = ledstate;
    kbd_put_ledstate(ledstate);
}

static void ps2_reset_keyboard(PS2KbdState *s)
{
    trace_ps2_reset_keyboard(s);
    s->scan_enabled = 1;
    s->scancode_set = 2;
    ps2_reset_queue(&s->common);
    ps2_set_ledstate(s, 0);
}

void ps2_write_keyboard(void *opaque, int val)
{
    PS2KbdState *s = (PS2KbdState *)opaque;

    trace_ps2_write_keyboard(opaque, val);
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
            ps2_queue(&s->common, KBD_REPLY_RESEND);
            break;
        }
        break;
    case KBD_CMD_SCANCODE:
        if (val == 0) {
            ps2_queue(&s->common, KBD_REPLY_ACK);
            ps2_put_keycode(s, s->scancode_set);
        } else if (val >= 1 && val <= 3) {
            s->scancode_set = val;
            ps2_queue(&s->common, KBD_REPLY_ACK);
        } else {
            ps2_queue(&s->common, KBD_REPLY_RESEND);
        }
        s->common.write_cmd = -1;
        break;
    case KBD_CMD_SET_LEDS:
        ps2_set_ledstate(s, val);
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
    trace_ps2_keyboard_set_translation(opaque, mode);
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

    trace_ps2_mouse_send_packet(s, dx1, dy1, dz1, b);
    /* update deltas */
    s->mouse_dx -= dx1;
    s->mouse_dy -= dy1;
    s->mouse_dz -= dz1;
}

static void ps2_mouse_event(DeviceState *dev, QemuConsole *src,
                            InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]   = PS2_MOUSE_BUTTON_LEFT,
        [INPUT_BUTTON_MIDDLE] = PS2_MOUSE_BUTTON_MIDDLE,
        [INPUT_BUTTON_RIGHT]  = PS2_MOUSE_BUTTON_RIGHT,
        [INPUT_BUTTON_SIDE]   = PS2_MOUSE_BUTTON_SIDE,
        [INPUT_BUTTON_EXTRA]  = PS2_MOUSE_BUTTON_EXTRA,
    };
    PS2MouseState *s = (PS2MouseState *)dev;
    InputMoveEvent *move;
    InputBtnEvent *btn;

    /* check if deltas are recorded when disabled */
    if (!(s->mouse_status & MOUSE_STATUS_ENABLED))
        return;

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            s->mouse_dx += move->value;
        } else if (move->axis == INPUT_AXIS_Y) {
            s->mouse_dy -= move->value;
        }
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            s->mouse_buttons |= bmap[btn->button];
            if (btn->button == INPUT_BUTTON_WHEEL_UP) {
                s->mouse_dz--;
            } else if (btn->button == INPUT_BUTTON_WHEEL_DOWN) {
                s->mouse_dz++;
            }
        } else {
            s->mouse_buttons &= ~bmap[btn->button];
        }
        break;

    default:
        /* keep gcc happy */
        break;
    }
}

static void ps2_mouse_sync(DeviceState *dev)
{
    PS2MouseState *s = (PS2MouseState *)dev;

    if (s->mouse_buttons) {
        qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER);
    }
    if (!(s->mouse_status & MOUSE_STATUS_REMOTE)) {
        while (s->common.queue.count < PS2_QUEUE_SIZE - 4) {
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
    PS2MouseState *s = opaque;
    trace_ps2_mouse_fake_event(opaque);
    s->mouse_dx++;
    ps2_mouse_sync(opaque);
}

void ps2_write_mouse(void *opaque, int val)
{
    PS2MouseState *s = (PS2MouseState *)opaque;

    trace_ps2_write_mouse(opaque, val);
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
    s->write_cmd = -1;
    ps2_reset_queue(s);
    s->update_irq(s->update_arg, 0);
}

static void ps2_common_post_load(PS2State *s)
{
    PS2Queue *q = &s->queue;
    int size;
    int i;
    int tmp_data[PS2_QUEUE_SIZE];

    /* set the useful data buffer queue size, < PS2_QUEUE_SIZE */
    size = q->count > PS2_QUEUE_SIZE ? 0 : q->count;

    /* move the queue elements to the start of data array */
    if (size > 0) {
        for (i = 0; i < size; i++) {
            /* move the queue elements to the temporary buffer */
            tmp_data[i] = q->data[q->rptr];
            if (++q->rptr == 256) {
                q->rptr = 0;
            }
        }
        memcpy(q->data, tmp_data, size);
    }
    /* reset rptr/wptr/count */
    q->rptr = 0;
    q->wptr = size;
    q->count = size;
    s->update_irq(s->update_arg, q->count != 0);
}

static void ps2_kbd_reset(void *opaque)
{
    PS2KbdState *s = (PS2KbdState *) opaque;

    trace_ps2_kbd_reset(opaque);
    ps2_common_reset(&s->common);
    s->scan_enabled = 0;
    s->translate = 0;
    s->scancode_set = 2;
}

static void ps2_mouse_reset(void *opaque)
{
    PS2MouseState *s = (PS2MouseState *) opaque;

    trace_ps2_mouse_reset(opaque);
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
    .fields = (VMStateField[]) {
        VMSTATE_INT32(write_cmd, PS2State),
        VMSTATE_INT32(queue.rptr, PS2State),
        VMSTATE_INT32(queue.wptr, PS2State),
        VMSTATE_INT32(queue.count, PS2State),
        VMSTATE_BUFFER(queue.data, PS2State),
        VMSTATE_END_OF_LIST()
    }
};

static bool ps2_keyboard_ledstate_needed(void *opaque)
{
    PS2KbdState *s = opaque;

    return s->ledstate != 0; /* 0 is default state */
}

static int ps2_kbd_ledstate_post_load(void *opaque, int version_id)
{
    PS2KbdState *s = opaque;

    kbd_put_ledstate(s->ledstate);
    return 0;
}

static const VMStateDescription vmstate_ps2_keyboard_ledstate = {
    .name = "ps2kbd/ledstate",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = ps2_kbd_ledstate_post_load,
    .needed = ps2_keyboard_ledstate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(ledstate, PS2KbdState),
        VMSTATE_END_OF_LIST()
    }
};

static bool ps2_keyboard_need_high_bit_needed(void *opaque)
{
    PS2KbdState *s = opaque;
    return s->need_high_bit != 0; /* 0 is the usual state */
}

static const VMStateDescription vmstate_ps2_keyboard_need_high_bit = {
    .name = "ps2kbd/need_high_bit",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ps2_keyboard_need_high_bit_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(need_high_bit, PS2KbdState),
        VMSTATE_END_OF_LIST()
    }
};

static int ps2_kbd_post_load(void* opaque, int version_id)
{
    PS2KbdState *s = (PS2KbdState*)opaque;
    PS2State *ps2 = &s->common;

    if (version_id == 2)
        s->scancode_set=2;

    ps2_common_post_load(ps2);

    return 0;
}

static void ps2_kbd_pre_save(void *opaque)
{
    PS2KbdState *s = (PS2KbdState *)opaque;
    PS2State *ps2 = &s->common;

    ps2_common_post_load(ps2);
}

static const VMStateDescription vmstate_ps2_keyboard = {
    .name = "ps2kbd",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = ps2_kbd_post_load,
    .pre_save = ps2_kbd_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(common, PS2KbdState, 0, vmstate_ps2_common, PS2State),
        VMSTATE_INT32(scan_enabled, PS2KbdState),
        VMSTATE_INT32(translate, PS2KbdState),
        VMSTATE_INT32_V(scancode_set, PS2KbdState,3),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_ps2_keyboard_ledstate,
        &vmstate_ps2_keyboard_need_high_bit,
        NULL
    }
};

static int ps2_mouse_post_load(void *opaque, int version_id)
{
    PS2MouseState *s = (PS2MouseState *)opaque;
    PS2State *ps2 = &s->common;

    ps2_common_post_load(ps2);

    return 0;
}

static void ps2_mouse_pre_save(void *opaque)
{
    PS2MouseState *s = (PS2MouseState *)opaque;
    PS2State *ps2 = &s->common;

    ps2_common_post_load(ps2);
}

static const VMStateDescription vmstate_ps2_mouse = {
    .name = "ps2mouse",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = ps2_mouse_post_load,
    .pre_save = ps2_mouse_pre_save,
    .fields = (VMStateField[]) {
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

static QemuInputHandler ps2_keyboard_handler = {
    .name  = "QEMU PS/2 Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = ps2_keyboard_event,
};

void *ps2_kbd_init(void (*update_irq)(void *, int), void *update_arg)
{
    PS2KbdState *s = (PS2KbdState *)g_malloc0(sizeof(PS2KbdState));

    trace_ps2_kbd_init(s);
    s->common.update_irq = update_irq;
    s->common.update_arg = update_arg;
    s->scancode_set = 2;
    vmstate_register(NULL, 0, &vmstate_ps2_keyboard, s);
    qemu_input_handler_register((DeviceState *)s,
                                &ps2_keyboard_handler);
    qemu_register_reset(ps2_kbd_reset, s);
    return s;
}

static QemuInputHandler ps2_mouse_handler = {
    .name  = "QEMU PS/2 Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = ps2_mouse_event,
    .sync  = ps2_mouse_sync,
};

void *ps2_mouse_init(void (*update_irq)(void *, int), void *update_arg)
{
    PS2MouseState *s = (PS2MouseState *)g_malloc0(sizeof(PS2MouseState));

    trace_ps2_mouse_init(s);
    s->common.update_irq = update_irq;
    s->common.update_arg = update_arg;
    vmstate_register(NULL, 0, &vmstate_ps2_mouse, s);
    qemu_input_handler_register((DeviceState *)s,
                                &ps2_mouse_handler);
    qemu_register_reset(ps2_mouse_reset, s);
    return s;
}
