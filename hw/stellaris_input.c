/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */
#include "hw.h"
#include "devices.h"
#include "console.h"

typedef struct {
    qemu_irq irq;
    int keycode;
    uint8_t pressed;
} gamepad_button;

typedef struct {
    gamepad_button *buttons;
    int num_buttons;
    int extension;
} gamepad_state;

static void stellaris_gamepad_put_key(void * opaque, int keycode)
{
    gamepad_state *s = (gamepad_state *)opaque;
    int i;
    int down;

    if (keycode == 0xe0 && !s->extension) {
        s->extension = 0x80;
        return;
    }

    down = (keycode & 0x80) == 0;
    keycode = (keycode & 0x7f) | s->extension;

    for (i = 0; i < s->num_buttons; i++) {
        if (s->buttons[i].keycode == keycode
                && s->buttons[i].pressed != down) {
            s->buttons[i].pressed = down;
            qemu_set_irq(s->buttons[i].irq, down);
        }
    }

    s->extension = 0;
}

static const VMStateDescription vmstate_stellaris_button = {
    .name = "stellaris_button",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(pressed, gamepad_button),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_stellaris_gamepad = {
    .name = "stellaris_gamepad",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(extension, gamepad_state),
        VMSTATE_STRUCT_VARRAY_INT32(buttons, gamepad_state, num_buttons, 0,
                              vmstate_stellaris_button, gamepad_button),
        VMSTATE_END_OF_LIST()
    }
};

/* Returns an array 5 ouput slots.  */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode)
{
    gamepad_state *s;
    int i;

    s = (gamepad_state *)qemu_mallocz(sizeof (gamepad_state));
    s->buttons = (gamepad_button *)qemu_mallocz(n * sizeof (gamepad_button));
    for (i = 0; i < n; i++) {
        s->buttons[i].irq = irq[i];
        s->buttons[i].keycode = keycode[i];
    }
    s->num_buttons = n;
    qemu_add_kbd_event_handler(stellaris_gamepad_put_key, s);
    vmstate_register(NULL, -1, &vmstate_stellaris_gamepad, s);
}
