/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "standard-headers/linux/input-event-codes.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/kbd-state.h"

struct QKbdState {
    QemuConsole *con;
    int key_delay_ms;
    DECLARE_BITMAP(keys, KEY_CNT);
    DECLARE_BITMAP(mods, QKBD_MOD__MAX);
};

static void qkbd_state_modifier_update(QKbdState *kbd,
                                       unsigned int lnx1, unsigned int lnx2,
                                      QKbdModifier mod)
{
    if (test_bit(lnx1, kbd->keys) || test_bit(lnx2, kbd->keys)) {
        set_bit(mod, kbd->mods);
    } else {
        clear_bit(mod, kbd->mods);
    }
}

bool qkbd_state_modifier_get(QKbdState *kbd, QKbdModifier mod)
{
    return test_bit(mod, kbd->mods);
}

bool qkbd_state_key_get(QKbdState *kbd, unsigned int lnx)
{
    return lnx < KEY_CNT && test_bit(lnx, kbd->keys);
}

void qkbd_state_key_event(QKbdState *kbd, unsigned int lnx, bool down)
{
    bool state;

    if (lnx >= KEY_CNT) {
        return;
    }

    state = test_bit(lnx, kbd->keys);

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
        set_bit(lnx, kbd->keys);
    } else {
        clear_bit(lnx, kbd->keys);
    }
    switch (lnx) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        qkbd_state_modifier_update(kbd, KEY_LEFTSHIFT, KEY_RIGHTSHIFT,
                                   QKBD_MOD_SHIFT);
        break;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        qkbd_state_modifier_update(kbd, KEY_LEFTCTRL, KEY_RIGHTCTRL,
                                   QKBD_MOD_CTRL);
        break;
    case KEY_LEFTALT:
        qkbd_state_modifier_update(kbd, KEY_LEFTALT, KEY_LEFTALT,
                                   QKBD_MOD_ALT);
        break;
    case KEY_RIGHTALT:
        qkbd_state_modifier_update(kbd, KEY_RIGHTALT, KEY_RIGHTALT,
                                   QKBD_MOD_ALTGR);
        break;
    case KEY_CAPSLOCK:
        if (down) {
            change_bit(QKBD_MOD_CAPSLOCK, kbd->mods);
        }
        break;
    case KEY_NUMLOCK:
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
        qemu_input_event_send_key_linux(kbd->con, lnx, down);
        if (kbd->key_delay_ms) {
            qemu_input_event_send_key_delay(kbd->key_delay_ms);
        }
    }
}

void qkbd_state_lift_all_keys(QKbdState *kbd)
{
    unsigned int lnx;

    for (lnx = 0; lnx < KEY_CNT; lnx++) {
        if (test_bit(lnx, kbd->keys)) {
            qkbd_state_key_event(kbd, lnx, false);
        }
    }
}

void qkbd_state_switch_console(QKbdState *kbd, QemuConsole *con)
{
    qkbd_state_lift_all_keys(kbd);
    kbd->con = con;
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
