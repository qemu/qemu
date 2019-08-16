/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/kbd-state.h"

struct QKbdState {
    QemuConsole *con;
    int key_delay_ms;
    DECLARE_BITMAP(keys, Q_KEY_CODE__MAX);
    DECLARE_BITMAP(mods, QKBD_MOD__MAX);
};

static void qkbd_state_modifier_update(QKbdState *kbd,
                                      QKeyCode qcode1, QKeyCode qcode2,
                                      QKbdModifier mod)
{
    if (test_bit(qcode1, kbd->keys) || test_bit(qcode2, kbd->keys)) {
        set_bit(mod, kbd->mods);
    } else {
        clear_bit(mod, kbd->mods);
    }
}

bool qkbd_state_modifier_get(QKbdState *kbd, QKbdModifier mod)
{
    return test_bit(mod, kbd->mods);
}

bool qkbd_state_key_get(QKbdState *kbd, QKeyCode qcode)
{
    return test_bit(qcode, kbd->keys);
}

void qkbd_state_key_event(QKbdState *kbd, QKeyCode qcode, bool down)
{
    bool state = test_bit(qcode, kbd->keys);

    if (down == false  /* got key-up event   */ &&
        state == false /* key is not pressed */) {
        /*
         * Filter out suspicious key-up events.
         *
         * This allows simply sending along all key-up events, and
         * this function will filter out everything where the
         * corresponding key-down event wasn't sent to the guest, for
         * example due to being a host hotkey.
         *
         * Note that key-down events on already pressed keys are *not*
         * suspicious, those are keyboard autorepeat events.
         */
        return;
    }

    /* update key and modifier state */
    if (down) {
        set_bit(qcode, kbd->keys);
    } else {
        clear_bit(qcode, kbd->keys);
    }
    switch (qcode) {
    case Q_KEY_CODE_SHIFT:
    case Q_KEY_CODE_SHIFT_R:
        qkbd_state_modifier_update(kbd, Q_KEY_CODE_SHIFT, Q_KEY_CODE_SHIFT_R,
                                   QKBD_MOD_SHIFT);
        break;
    case Q_KEY_CODE_CTRL:
    case Q_KEY_CODE_CTRL_R:
        qkbd_state_modifier_update(kbd, Q_KEY_CODE_CTRL, Q_KEY_CODE_CTRL_R,
                                   QKBD_MOD_CTRL);
        break;
    case Q_KEY_CODE_ALT:
        qkbd_state_modifier_update(kbd, Q_KEY_CODE_ALT, Q_KEY_CODE_ALT,
                                   QKBD_MOD_ALT);
        break;
    case Q_KEY_CODE_ALT_R:
        qkbd_state_modifier_update(kbd, Q_KEY_CODE_ALT_R, Q_KEY_CODE_ALT_R,
                                   QKBD_MOD_ALTGR);
        break;
    case Q_KEY_CODE_CAPS_LOCK:
        if (down) {
            change_bit(QKBD_MOD_CAPSLOCK, kbd->mods);
        }
        break;
    case Q_KEY_CODE_NUM_LOCK:
        if (down) {
            change_bit(QKBD_MOD_NUMLOCK, kbd->mods);
        }
        break;
    default:
        /* keep gcc happy */
        break;
    }

    /* send to guest */
    if (qemu_console_is_graphic(kbd->con)) {
        qemu_input_event_send_key_qcode(kbd->con, qcode, down);
        if (kbd->key_delay_ms) {
            qemu_input_event_send_key_delay(kbd->key_delay_ms);
        }
    }
}

void qkbd_state_lift_all_keys(QKbdState *kbd)
{
    int qcode;

    for (qcode = 0; qcode < Q_KEY_CODE__MAX; qcode++) {
        if (test_bit(qcode, kbd->keys)) {
            qkbd_state_key_event(kbd, qcode, false);
        }
    }
}

void qkbd_state_set_delay(QKbdState *kbd, int delay_ms)
{
    kbd->key_delay_ms = delay_ms;
}

void qkbd_state_free(QKbdState *kbd)
{
    g_free(kbd);
}

QKbdState *qkbd_state_init(QemuConsole *con)
{
    QKbdState *kbd = g_new0(QKbdState, 1);

    kbd->con = con;

    return kbd;
}
