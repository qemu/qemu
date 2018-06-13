#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "keymaps.h"
#include "ui/input.h"

#include "standard-headers/linux/input.h"

#include "ui/input-keymap-atset1-to-qcode.c"
#include "ui/input-keymap-linux-to-qcode.c"
#include "ui/input-keymap-qcode-to-atset1.c"
#include "ui/input-keymap-qcode-to-atset2.c"
#include "ui/input-keymap-qcode-to-atset3.c"
#include "ui/input-keymap-qcode-to-linux.c"
#include "ui/input-keymap-qcode-to-qnum.c"
#include "ui/input-keymap-qcode-to-sun.c"
#include "ui/input-keymap-qnum-to-qcode.c"
#include "ui/input-keymap-usb-to-qcode.c"
#include "ui/input-keymap-win32-to-qcode.c"
#include "ui/input-keymap-x11-to-qcode.c"
#include "ui/input-keymap-xorgevdev-to-qcode.c"
#include "ui/input-keymap-xorgkbd-to-qcode.c"
#include "ui/input-keymap-xorgxquartz-to-qcode.c"
#include "ui/input-keymap-xorgxwin-to-qcode.c"
#include "ui/input-keymap-osx-to-qcode.c"

int qemu_input_linux_to_qcode(unsigned int lnx)
{
    if (lnx >= qemu_input_map_linux_to_qcode_len) {
        return 0;
    }
    return qemu_input_map_linux_to_qcode[lnx];
}

int qemu_input_key_value_to_number(const KeyValue *value)
{
    if (value->type == KEY_VALUE_KIND_QCODE) {
        if (value->u.qcode.data >= qemu_input_map_qcode_to_qnum_len) {
            return 0;
        }
        return qemu_input_map_qcode_to_qnum[value->u.qcode.data];
    } else {
        assert(value->type == KEY_VALUE_KIND_NUMBER);
        return value->u.number.data;
    }
}

int qemu_input_key_number_to_qcode(unsigned int nr)
{
    if (nr >= qemu_input_map_qnum_to_qcode_len) {
        return 0;
    }
    return qemu_input_map_qnum_to_qcode[nr];
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
