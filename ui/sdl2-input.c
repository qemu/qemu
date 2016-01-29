/*
 * QEMU SDL display driver
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
/* Ported SDL 1.2 code to 2.0 by Dave Airlie. */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/sdl2.h"
#include "sysemu/sysemu.h"

#include "sdl2-keymap.h"

static uint8_t modifiers_state[SDL_NUM_SCANCODES];

void sdl2_reset_keys(struct sdl2_console *scon)
{
    QemuConsole *con = scon ? scon->dcl.con : NULL;
    int i;

    for (i = 0; i < SDL_NUM_SCANCODES; i++) {
        if (modifiers_state[i]) {
            int qcode = sdl2_scancode_to_qcode[i];
            qemu_input_event_send_key_qcode(con, qcode, false);
            modifiers_state[i] = 0;
        }
    }
}

void sdl2_process_key(struct sdl2_console *scon,
                      SDL_KeyboardEvent *ev)
{
    int qcode = sdl2_scancode_to_qcode[ev->keysym.scancode];
    QemuConsole *con = scon ? scon->dcl.con : NULL;

    if (!qemu_console_is_graphic(con)) {
        if (ev->type == SDL_KEYDOWN) {
            switch (ev->keysym.scancode) {
            case SDL_SCANCODE_RETURN:
                kbd_put_keysym_console(con, '\n');
                break;
            case SDL_SCANCODE_BACKSPACE:
                kbd_put_keysym_console(con, QEMU_KEY_BACKSPACE);
                break;
            default:
                kbd_put_qcode_console(con, qcode);
                break;
            }
        }
        return;
    }

    switch (ev->keysym.scancode) {
#if 0
    case SDL_SCANCODE_NUMLOCKCLEAR:
    case SDL_SCANCODE_CAPSLOCK:
        /* SDL does not send the key up event, so we generate it */
        qemu_input_event_send_key_qcode(con, qcode, true);
        qemu_input_event_send_key_qcode(con, qcode, false);
        return;
#endif
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_RGUI:
        if (ev->type == SDL_KEYUP) {
            modifiers_state[ev->keysym.scancode] = 0;
        } else {
            modifiers_state[ev->keysym.scancode] = 1;
        }
        /* fall though */
    default:
        qemu_input_event_send_key_qcode(con, qcode,
                                        ev->type == SDL_KEYDOWN);
    }
}
