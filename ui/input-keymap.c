#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "ui/keymaps.h"
#include "ui/input.h"

#include "standard-headers/linux/input.h"

static int linux_to_qcode[KEY_CNT] = {
    [KEY_ESC]            = Q_KEY_CODE_ESC,
    [KEY_1]              = Q_KEY_CODE_1,
    [KEY_2]              = Q_KEY_CODE_2,
    [KEY_3]              = Q_KEY_CODE_3,
    [KEY_4]              = Q_KEY_CODE_4,
    [KEY_5]              = Q_KEY_CODE_5,
    [KEY_6]              = Q_KEY_CODE_6,
    [KEY_7]              = Q_KEY_CODE_7,
    [KEY_8]              = Q_KEY_CODE_8,
    [KEY_9]              = Q_KEY_CODE_9,
    [KEY_0]              = Q_KEY_CODE_0,
    [KEY_MINUS]          = Q_KEY_CODE_MINUS,
    [KEY_EQUAL]          = Q_KEY_CODE_EQUAL,
    [KEY_BACKSPACE]      = Q_KEY_CODE_BACKSPACE,
    [KEY_TAB]            = Q_KEY_CODE_TAB,
    [KEY_Q]              = Q_KEY_CODE_Q,
    [KEY_W]              = Q_KEY_CODE_W,
    [KEY_E]              = Q_KEY_CODE_E,
    [KEY_R]              = Q_KEY_CODE_R,
    [KEY_T]              = Q_KEY_CODE_T,
    [KEY_Y]              = Q_KEY_CODE_Y,
    [KEY_U]              = Q_KEY_CODE_U,
    [KEY_I]              = Q_KEY_CODE_I,
    [KEY_O]              = Q_KEY_CODE_O,
    [KEY_P]              = Q_KEY_CODE_P,
    [KEY_LEFTBRACE]      = Q_KEY_CODE_BRACKET_LEFT,
    [KEY_RIGHTBRACE]     = Q_KEY_CODE_BRACKET_RIGHT,
    [KEY_ENTER]          = Q_KEY_CODE_RET,
    [KEY_LEFTCTRL]       = Q_KEY_CODE_CTRL,
    [KEY_A]              = Q_KEY_CODE_A,
    [KEY_S]              = Q_KEY_CODE_S,
    [KEY_D]              = Q_KEY_CODE_D,
    [KEY_F]              = Q_KEY_CODE_F,
    [KEY_G]              = Q_KEY_CODE_G,
    [KEY_H]              = Q_KEY_CODE_H,
    [KEY_J]              = Q_KEY_CODE_J,
    [KEY_K]              = Q_KEY_CODE_K,
    [KEY_L]              = Q_KEY_CODE_L,
    [KEY_SEMICOLON]      = Q_KEY_CODE_SEMICOLON,
    [KEY_APOSTROPHE]     = Q_KEY_CODE_APOSTROPHE,
    [KEY_GRAVE]          = Q_KEY_CODE_GRAVE_ACCENT,
    [KEY_LEFTSHIFT]      = Q_KEY_CODE_SHIFT,
    [KEY_BACKSLASH]      = Q_KEY_CODE_BACKSLASH,
    [KEY_102ND]          = Q_KEY_CODE_LESS,
    [KEY_Z]              = Q_KEY_CODE_Z,
    [KEY_X]              = Q_KEY_CODE_X,
    [KEY_C]              = Q_KEY_CODE_C,
    [KEY_V]              = Q_KEY_CODE_V,
    [KEY_B]              = Q_KEY_CODE_B,
    [KEY_N]              = Q_KEY_CODE_N,
    [KEY_M]              = Q_KEY_CODE_M,
    [KEY_COMMA]          = Q_KEY_CODE_COMMA,
    [KEY_DOT]            = Q_KEY_CODE_DOT,
    [KEY_SLASH]          = Q_KEY_CODE_SLASH,
    [KEY_RIGHTSHIFT]     = Q_KEY_CODE_SHIFT_R,
    [KEY_LEFTALT]        = Q_KEY_CODE_ALT,
    [KEY_SPACE]          = Q_KEY_CODE_SPC,
    [KEY_CAPSLOCK]       = Q_KEY_CODE_CAPS_LOCK,
    [KEY_F1]             = Q_KEY_CODE_F1,
    [KEY_F2]             = Q_KEY_CODE_F2,
    [KEY_F3]             = Q_KEY_CODE_F3,
    [KEY_F4]             = Q_KEY_CODE_F4,
    [KEY_F5]             = Q_KEY_CODE_F5,
    [KEY_F6]             = Q_KEY_CODE_F6,
    [KEY_F7]             = Q_KEY_CODE_F7,
    [KEY_F8]             = Q_KEY_CODE_F8,
    [KEY_F9]             = Q_KEY_CODE_F9,
    [KEY_F10]            = Q_KEY_CODE_F10,
    [KEY_NUMLOCK]        = Q_KEY_CODE_NUM_LOCK,
    [KEY_SCROLLLOCK]     = Q_KEY_CODE_SCROLL_LOCK,
    [KEY_KP0]            = Q_KEY_CODE_KP_0,
    [KEY_KP1]            = Q_KEY_CODE_KP_1,
    [KEY_KP2]            = Q_KEY_CODE_KP_2,
    [KEY_KP3]            = Q_KEY_CODE_KP_3,
    [KEY_KP4]            = Q_KEY_CODE_KP_4,
    [KEY_KP5]            = Q_KEY_CODE_KP_5,
    [KEY_KP6]            = Q_KEY_CODE_KP_6,
    [KEY_KP7]            = Q_KEY_CODE_KP_7,
    [KEY_KP8]            = Q_KEY_CODE_KP_8,
    [KEY_KP9]            = Q_KEY_CODE_KP_9,
    [KEY_KPMINUS]        = Q_KEY_CODE_KP_SUBTRACT,
    [KEY_KPPLUS]         = Q_KEY_CODE_KP_ADD,
    [KEY_KPDOT]          = Q_KEY_CODE_KP_DECIMAL,
    [KEY_KPENTER]        = Q_KEY_CODE_KP_ENTER,
    [KEY_KPSLASH]        = Q_KEY_CODE_KP_DIVIDE,
    [KEY_KPASTERISK]     = Q_KEY_CODE_KP_MULTIPLY,
    [KEY_F11]            = Q_KEY_CODE_F11,
    [KEY_F12]            = Q_KEY_CODE_F12,
    [KEY_RO]             = Q_KEY_CODE_RO,
    [KEY_HIRAGANA]       = Q_KEY_CODE_HIRAGANA,
    [KEY_HENKAN]         = Q_KEY_CODE_HENKAN,
    [KEY_RIGHTCTRL]      = Q_KEY_CODE_CTRL_R,
    [KEY_SYSRQ]          = Q_KEY_CODE_SYSRQ,
    [KEY_RIGHTALT]       = Q_KEY_CODE_ALT_R,
    [KEY_HOME]           = Q_KEY_CODE_HOME,
    [KEY_UP]             = Q_KEY_CODE_UP,
    [KEY_PAGEUP]         = Q_KEY_CODE_PGUP,
    [KEY_LEFT]           = Q_KEY_CODE_LEFT,
    [KEY_RIGHT]          = Q_KEY_CODE_RIGHT,
    [KEY_END]            = Q_KEY_CODE_END,
    [KEY_DOWN]           = Q_KEY_CODE_DOWN,
    [KEY_PAGEDOWN]       = Q_KEY_CODE_PGDN,
    [KEY_INSERT]         = Q_KEY_CODE_INSERT,
    [KEY_DELETE]         = Q_KEY_CODE_DELETE,
    [KEY_POWER]          = Q_KEY_CODE_POWER,
    [KEY_KPCOMMA]        = Q_KEY_CODE_KP_COMMA,
    [KEY_YEN]            = Q_KEY_CODE_YEN,
    [KEY_LEFTMETA]       = Q_KEY_CODE_META_L,
    [KEY_RIGHTMETA]      = Q_KEY_CODE_META_R,
    [KEY_MENU]           = Q_KEY_CODE_MENU,
    [KEY_PAUSE]          = Q_KEY_CODE_PAUSE,

    [KEY_SLEEP]          = Q_KEY_CODE_SLEEP,
    [KEY_WAKEUP]         = Q_KEY_CODE_WAKE,
    [KEY_CALC]           = Q_KEY_CODE_CALCULATOR,
    [KEY_MAIL]           = Q_KEY_CODE_MAIL,
    [KEY_COMPUTER]       = Q_KEY_CODE_COMPUTER,

    [KEY_STOP]           = Q_KEY_CODE_STOP,
    [KEY_BOOKMARKS]      = Q_KEY_CODE_AC_BOOKMARKS,
    [KEY_BACK]           = Q_KEY_CODE_AC_BACK,
    [KEY_FORWARD]        = Q_KEY_CODE_AC_FORWARD,
    [KEY_HOMEPAGE]       = Q_KEY_CODE_AC_HOME,
    [KEY_REFRESH]        = Q_KEY_CODE_AC_REFRESH,
    [KEY_FIND]           = Q_KEY_CODE_FIND,

    [KEY_NEXTSONG]       = Q_KEY_CODE_AUDIONEXT,
    [KEY_PREVIOUSSONG]   = Q_KEY_CODE_AUDIOPREV,
    [KEY_STOPCD]         = Q_KEY_CODE_AUDIOSTOP,
    [KEY_PLAYCD]         = Q_KEY_CODE_AUDIOPLAY,
    [KEY_MUTE]           = Q_KEY_CODE_AUDIOMUTE,
    [KEY_VOLUMEDOWN]     = Q_KEY_CODE_VOLUMEDOWN,
    [KEY_VOLUMEUP]       = Q_KEY_CODE_VOLUMEUP,
};

static const int qcode_to_number[] = {
    [Q_KEY_CODE_SHIFT] = 0x2a,
    [Q_KEY_CODE_SHIFT_R] = 0x36,

    [Q_KEY_CODE_ALT] = 0x38,
    [Q_KEY_CODE_ALT_R] = 0xb8,
    [Q_KEY_CODE_CTRL] = 0x1d,
    [Q_KEY_CODE_CTRL_R] = 0x9d,

    [Q_KEY_CODE_META_L] = 0xdb,
    [Q_KEY_CODE_META_R] = 0xdc,
    [Q_KEY_CODE_MENU] = 0xdd,

    [Q_KEY_CODE_ESC] = 0x01,

    [Q_KEY_CODE_1] = 0x02,
    [Q_KEY_CODE_2] = 0x03,
    [Q_KEY_CODE_3] = 0x04,
    [Q_KEY_CODE_4] = 0x05,
    [Q_KEY_CODE_5] = 0x06,
    [Q_KEY_CODE_6] = 0x07,
    [Q_KEY_CODE_7] = 0x08,
    [Q_KEY_CODE_8] = 0x09,
    [Q_KEY_CODE_9] = 0x0a,
    [Q_KEY_CODE_0] = 0x0b,
    [Q_KEY_CODE_MINUS] = 0x0c,
    [Q_KEY_CODE_EQUAL] = 0x0d,
    [Q_KEY_CODE_BACKSPACE] = 0x0e,

    [Q_KEY_CODE_TAB] = 0x0f,
    [Q_KEY_CODE_Q] = 0x10,
    [Q_KEY_CODE_W] = 0x11,
    [Q_KEY_CODE_E] = 0x12,
    [Q_KEY_CODE_R] = 0x13,
    [Q_KEY_CODE_T] = 0x14,
    [Q_KEY_CODE_Y] = 0x15,
    [Q_KEY_CODE_U] = 0x16,
    [Q_KEY_CODE_I] = 0x17,
    [Q_KEY_CODE_O] = 0x18,
    [Q_KEY_CODE_P] = 0x19,
    [Q_KEY_CODE_BRACKET_LEFT] = 0x1a,
    [Q_KEY_CODE_BRACKET_RIGHT] = 0x1b,
    [Q_KEY_CODE_RET] = 0x1c,

    [Q_KEY_CODE_A] = 0x1e,
    [Q_KEY_CODE_S] = 0x1f,
    [Q_KEY_CODE_D] = 0x20,
    [Q_KEY_CODE_F] = 0x21,
    [Q_KEY_CODE_G] = 0x22,
    [Q_KEY_CODE_H] = 0x23,
    [Q_KEY_CODE_J] = 0x24,
    [Q_KEY_CODE_K] = 0x25,
    [Q_KEY_CODE_L] = 0x26,
    [Q_KEY_CODE_SEMICOLON] = 0x27,
    [Q_KEY_CODE_APOSTROPHE] = 0x28,
    [Q_KEY_CODE_GRAVE_ACCENT] = 0x29,

    [Q_KEY_CODE_BACKSLASH] = 0x2b,
    [Q_KEY_CODE_Z] = 0x2c,
    [Q_KEY_CODE_X] = 0x2d,
    [Q_KEY_CODE_C] = 0x2e,
    [Q_KEY_CODE_V] = 0x2f,
    [Q_KEY_CODE_B] = 0x30,
    [Q_KEY_CODE_N] = 0x31,
    [Q_KEY_CODE_M] = 0x32,
    [Q_KEY_CODE_COMMA] = 0x33,
    [Q_KEY_CODE_DOT] = 0x34,
    [Q_KEY_CODE_SLASH] = 0x35,

    [Q_KEY_CODE_ASTERISK] = 0x37,

    [Q_KEY_CODE_SPC] = 0x39,
    [Q_KEY_CODE_CAPS_LOCK] = 0x3a,
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
    [Q_KEY_CODE_NUM_LOCK] = 0x45,
    [Q_KEY_CODE_SCROLL_LOCK] = 0x46,

    [Q_KEY_CODE_KP_DIVIDE] = 0xb5,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x37,
    [Q_KEY_CODE_KP_SUBTRACT] = 0x4a,
    [Q_KEY_CODE_KP_ADD] = 0x4e,
    [Q_KEY_CODE_KP_ENTER] = 0x9c,
    [Q_KEY_CODE_KP_DECIMAL] = 0x53,
    [Q_KEY_CODE_SYSRQ] = 0x54,
    [Q_KEY_CODE_PAUSE] = 0xc6,

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

    [Q_KEY_CODE_LESS] = 0x56,

    [Q_KEY_CODE_F11] = 0x57,
    [Q_KEY_CODE_F12] = 0x58,

    [Q_KEY_CODE_PRINT] = 0xb7,

    [Q_KEY_CODE_HOME] = 0xc7,
    [Q_KEY_CODE_PGUP] = 0xc9,
    [Q_KEY_CODE_PGDN] = 0xd1,
    [Q_KEY_CODE_END] = 0xcf,

    [Q_KEY_CODE_LEFT] = 0xcb,
    [Q_KEY_CODE_UP] = 0xc8,
    [Q_KEY_CODE_DOWN] = 0xd0,
    [Q_KEY_CODE_RIGHT] = 0xcd,

    [Q_KEY_CODE_INSERT] = 0xd2,
    [Q_KEY_CODE_DELETE] = 0xd3,

    [Q_KEY_CODE_RO] = 0x73,
    [Q_KEY_CODE_HIRAGANA] = 0x70,
    [Q_KEY_CODE_HENKAN] = 0x79,
    [Q_KEY_CODE_POWER] = 0xde,
    [Q_KEY_CODE_YEN] = 0x7d,
    [Q_KEY_CODE_KP_COMMA] = 0x7e,

    [Q_KEY_CODE_SLEEP] = 0xdf,
    [Q_KEY_CODE_WAKE] = 0xe3,
    [Q_KEY_CODE_CALCULATOR] = 0xa1,
    [Q_KEY_CODE_MAIL] = 0xec,
    [Q_KEY_CODE_COMPUTER] = 0xeb,

    [Q_KEY_CODE_STOP] = 0xe8,
    [Q_KEY_CODE_AC_BOOKMARKS] = 0xe6,
    [Q_KEY_CODE_AC_BACK] = 0xea,
    [Q_KEY_CODE_AC_FORWARD] = 0xe9,
    [Q_KEY_CODE_AC_HOME] = 0xb2,
    [Q_KEY_CODE_AC_REFRESH] = 0xe7,
    [Q_KEY_CODE_FIND] = 0xe5,

    [Q_KEY_CODE_AUDIONEXT] = 0x99,
    [Q_KEY_CODE_AUDIOPREV] = 0x90,
    [Q_KEY_CODE_AUDIOSTOP] = 0xa4,
    [Q_KEY_CODE_AUDIOPLAY] = 0xa2,
    [Q_KEY_CODE_AUDIOMUTE] = 0xa0,
    [Q_KEY_CODE_VOLUMEDOWN] = 0xae,
    [Q_KEY_CODE_VOLUMEUP] = 0xb0,

    [Q_KEY_CODE__MAX] = 0,
};

static int number_to_qcode[0x100];

int qemu_input_linux_to_qcode(unsigned int lnx)
{
    assert(lnx < KEY_CNT);
    return linux_to_qcode[lnx];
}

int qemu_input_key_value_to_number(const KeyValue *value)
{
    if (value->type == KEY_VALUE_KIND_QCODE) {
        return qcode_to_number[value->u.qcode.data];
    } else {
        assert(value->type == KEY_VALUE_KIND_NUMBER);
        return value->u.number.data;
    }
}

int qemu_input_key_number_to_qcode(uint8_t nr)
{
    static int first = true;

    if (first) {
        int qcode, number;
        first = false;
        for (qcode = 0; qcode < Q_KEY_CODE__MAX; qcode++) {
            number = qcode_to_number[qcode];
            assert(number < ARRAY_SIZE(number_to_qcode));
            number_to_qcode[number] = qcode;
        }
    }

    return number_to_qcode[nr];
}

int qemu_input_key_value_to_qcode(const KeyValue *value)
{
    if (value->type == KEY_VALUE_KIND_QCODE) {
        return value->u.qcode.data;
    } else {
        assert(value->type == KEY_VALUE_KIND_NUMBER);
        return qemu_input_key_number_to_qcode(value->u.number.data);
    }
}

int qemu_input_key_value_to_scancode(const KeyValue *value, bool down,
                                     int *codes)
{
    int keycode = qemu_input_key_value_to_number(value);
    int count = 0;

    if (value->type == KEY_VALUE_KIND_QCODE &&
        value->u.qcode.data == Q_KEY_CODE_PAUSE) {
        /* specific case */
        int v = down ? 0 : 0x80;
        codes[count++] = 0xe1;
        codes[count++] = 0x1d | v;
        codes[count++] = 0x45 | v;
        return count;
    }
    if (keycode & SCANCODE_GREY) {
        codes[count++] = SCANCODE_EMUL0;
        keycode &= ~SCANCODE_GREY;
    }
    if (!down) {
        keycode |= SCANCODE_UP;
    }
    codes[count++] = keycode;

    return count;
}
