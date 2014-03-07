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
#include <stdbool.h>
#include <string.h>

#include <spice.h>
#include <spice/enums.h>

#include "qemu-common.h"
#include "ui/qemu-spice.h"
#include "ui/console.h"
#include "ui/keymaps.h"
#include "ui/input.h"

/* keyboard bits */

typedef struct QemuSpiceKbd {
    SpiceKbdInstance sin;
    int ledstate;
    bool emul0;
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

static void kbd_push_key(SpiceKbdInstance *sin, uint8_t scancode)
{
    QemuSpiceKbd *kbd = container_of(sin, QemuSpiceKbd, sin);
    int keycode;
    bool up;

    if (scancode == SCANCODE_EMUL0) {
        kbd->emul0 = true;
        return;
    }
    keycode = scancode & ~SCANCODE_UP;
    up = scancode & SCANCODE_UP;
    if (kbd->emul0) {
        kbd->emul0 = false;
        keycode |= SCANCODE_GREY;
    }

    qemu_input_event_send_key_number(NULL, keycode, !up);
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

/* mouse bits */

typedef struct QemuSpicePointer {
    SpiceMouseInstance  mouse;
    SpiceTabletInstance tablet;
    int width, height;
    uint32_t last_bmask;
    Notifier mouse_mode;
    bool absolute;
} QemuSpicePointer;

static void spice_update_buttons(QemuSpicePointer *pointer,
                                 int wheel, uint32_t button_mask)
{
    static uint32_t bmap[INPUT_BUTTON_MAX] = {
        [INPUT_BUTTON_LEFT]        = 0x01,
        [INPUT_BUTTON_MIDDLE]      = 0x04,
        [INPUT_BUTTON_RIGHT]       = 0x02,
        [INPUT_BUTTON_WHEEL_UP]    = 0x10,
        [INPUT_BUTTON_WHEEL_DOWN]  = 0x20,
    };

    if (wheel < 0) {
        button_mask |= 0x10;
    }
    if (wheel > 0) {
        button_mask |= 0x20;
    }

    if (pointer->last_bmask == button_mask) {
        return;
    }
    qemu_input_update_buttons(NULL, bmap, pointer->last_bmask, button_mask);
    pointer->last_bmask = button_mask;
}

static void mouse_motion(SpiceMouseInstance *sin, int dx, int dy, int dz,
                         uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, mouse);
    spice_update_buttons(pointer, dz, buttons_state);
    qemu_input_queue_rel(NULL, INPUT_AXIS_X, dx);
    qemu_input_queue_rel(NULL, INPUT_AXIS_Y, dy);
    qemu_input_event_sync();
}

static void mouse_buttons(SpiceMouseInstance *sin, uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, mouse);
    spice_update_buttons(pointer, 0, buttons_state);
    qemu_input_event_sync();
}

static const SpiceMouseInterface mouse_interface = {
    .base.type          = SPICE_INTERFACE_MOUSE,
    .base.description   = "mouse",
    .base.major_version = SPICE_INTERFACE_MOUSE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_MOUSE_MINOR,
    .motion             = mouse_motion,
    .buttons            = mouse_buttons,
};

static void tablet_set_logical_size(SpiceTabletInstance* sin, int width, int height)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    if (height < 16) {
        height = 16;
    }
    if (width < 16) {
        width = 16;
    }
    pointer->width  = width;
    pointer->height = height;
}

static void tablet_position(SpiceTabletInstance* sin, int x, int y,
                            uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    spice_update_buttons(pointer, 0, buttons_state);
    qemu_input_queue_abs(NULL, INPUT_AXIS_X, x, pointer->width);
    qemu_input_queue_abs(NULL, INPUT_AXIS_Y, y, pointer->width);
    qemu_input_event_sync();
}


static void tablet_wheel(SpiceTabletInstance* sin, int wheel,
                         uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    spice_update_buttons(pointer, wheel, buttons_state);
    qemu_input_event_sync();
}

static void tablet_buttons(SpiceTabletInstance *sin,
                           uint32_t buttons_state)
{
    QemuSpicePointer *pointer = container_of(sin, QemuSpicePointer, tablet);

    spice_update_buttons(pointer, 0, buttons_state);
    qemu_input_event_sync();
}

static const SpiceTabletInterface tablet_interface = {
    .base.type          = SPICE_INTERFACE_TABLET,
    .base.description   = "tablet",
    .base.major_version = SPICE_INTERFACE_TABLET_MAJOR,
    .base.minor_version = SPICE_INTERFACE_TABLET_MINOR,
    .set_logical_size   = tablet_set_logical_size,
    .position           = tablet_position,
    .wheel              = tablet_wheel,
    .buttons            = tablet_buttons,
};

static void mouse_mode_notifier(Notifier *notifier, void *data)
{
    QemuSpicePointer *pointer = container_of(notifier, QemuSpicePointer, mouse_mode);
    bool is_absolute  = qemu_input_is_absolute();

    if (pointer->absolute == is_absolute) {
        return;
    }

    if (is_absolute) {
        qemu_spice_add_interface(&pointer->tablet.base);
    } else {
        spice_server_remove_interface(&pointer->tablet.base);
    }
    pointer->absolute = is_absolute;
}

void qemu_spice_input_init(void)
{
    QemuSpiceKbd *kbd;
    QemuSpicePointer *pointer;

    kbd = g_malloc0(sizeof(*kbd));
    kbd->sin.base.sif = &kbd_interface.base;
    qemu_spice_add_interface(&kbd->sin.base);
    qemu_add_led_event_handler(kbd_leds, kbd);

    pointer = g_malloc0(sizeof(*pointer));
    pointer->mouse.base.sif  = &mouse_interface.base;
    pointer->tablet.base.sif = &tablet_interface.base;
    qemu_spice_add_interface(&pointer->mouse.base);

    pointer->absolute = false;
    pointer->mouse_mode.notify = mouse_mode_notifier;
    qemu_add_mouse_mode_change_notifier(&pointer->mouse_mode);
    mouse_mode_notifier(&pointer->mouse_mode, NULL);
}
