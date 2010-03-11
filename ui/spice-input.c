/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <spice.h>
#include <spice/enums.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "console.h"

/* keyboard bits */

typedef struct QemuSpiceKbd {
    SpiceKbdInstance sin;
    int ledstate;
} QemuSpiceKbd;

static void kbd_push_key(SpiceKbdInstance *sin, uint8_t frag);
static uint8_t kbd_get_leds(SpiceKbdInstance *sin);
static void kbd_leds(void *opaque, int l);

static const SpiceKbdInterface kbd_interface = {
    .base.type          = SPICE_INTERFACE_KEYBOARD,
    .base.description   = "qemu keyboard",
    .base.major_version = SPICE_INTERFACE_KEYBOARD_MAJOR,
    .base.minor_version = SPICE_INTERFACE_KEYBOARD_MINOR,
    .push_scan_freg     = kbd_push_key,
    .get_leds           = kbd_get_leds,
};

static void kbd_push_key(SpiceKbdInstance *sin, uint8_t frag)
{
    kbd_put_keycode(frag);
}

static uint8_t kbd_get_leds(SpiceKbdInstance *sin)
{
    QemuSpiceKbd *kbd = container_of(sin, QemuSpiceKbd, sin);
    return kbd->ledstate;
}

static void kbd_leds(void *opaque, int ledstate)
{
    QemuSpiceKbd *kbd = opaque;

    kbd->ledstate = 0;
    if (ledstate & QEMU_SCROLL_LOCK_LED) {
        kbd->ledstate |= SPICE_KEYBOARD_MODIFIER_FLAGS_SCROLL_LOCK;
    }
    if (ledstate & QEMU_NUM_LOCK_LED) {
        kbd->ledstate |= SPICE_KEYBOARD_MODIFIER_FLAGS_NUM_LOCK;
    }
    if (ledstate & QEMU_CAPS_LOCK_LED) {
        kbd->ledstate |= SPICE_KEYBOARD_MODIFIER_FLAGS_CAPS_LOCK;
    }
    spice_server_kbd_leds(&kbd->sin, ledstate);
}

void qemu_spice_input_init(void)
{
    QemuSpiceKbd *kbd;

    kbd = qemu_mallocz(sizeof(*kbd));
    kbd->sin.base.sif = &kbd_interface.base;
    qemu_spice_add_interface(&kbd->sin.base);
    qemu_add_led_event_handler(kbd_leds, kbd);
}
