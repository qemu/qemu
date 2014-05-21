#include "sysemu/sysemu.h"
#include "ui/keymaps.h"
#include "ui/input.h"

static const int qcode_to_number[] = {
    [Q_KEY_CODE_SHIFT] = 0x2a,
    [Q_KEY_CODE_SHIFT_R] = 0x36,

    [Q_KEY_CODE_ALT] = 0x38,
    [Q_KEY_CODE_ALT_R] = 0xb8,
    [Q_KEY_CODE_ALTGR] = 0x64,
    [Q_KEY_CODE_ALTGR_R] = 0xe4,
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
    [Q_KEY_CODE_MAX] = 0,
};

static int number_to_qcode[0x100];

int qemu_input_key_value_to_number(const KeyValue *value)
{
    if (value->kind == KEY_VALUE_KIND_QCODE) {
        return qcode_to_number[value->qcode];
    } else {
        assert(value->kind == KEY_VALUE_KIND_NUMBER);
        return value->number;
    }
}

int qemu_input_key_number_to_qcode(uint8_t nr)
{
    static int first = true;

    if (first) {
        int qcode, number;
        first = false;
        for (qcode = 0; qcode < Q_KEY_CODE_MAX; qcode++) {
            number = qcode_to_number[qcode];
            assert(number < ARRAY_SIZE(number_to_qcode));
            number_to_qcode[number] = qcode;
        }
    }

    return number_to_qcode[nr];
}

int qemu_input_key_value_to_qcode(const KeyValue *value)
{
    if (value->kind == KEY_VALUE_KIND_QCODE) {
        return value->qcode;
    } else {
        assert(value->kind == KEY_VALUE_KIND_NUMBER);
        return qemu_input_key_number_to_qcode(value->number);
    }
}

int qemu_input_key_value_to_scancode(const KeyValue *value, bool down,
                                     int *codes)
{
    int keycode = qemu_input_key_value_to_number(value);
    int count = 0;

    if (value->kind == KEY_VALUE_KIND_QCODE &&
        value->qcode == Q_KEY_CODE_PAUSE) {
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
