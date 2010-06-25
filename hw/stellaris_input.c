/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */
#include "hw.h"
#include "devices.h"
#include "console.h"

typedef struct {
    qemu_irq irq;
    int keycode;
    int pressed;
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

static void stellaris_gamepad_save(QEMUFile *f, void *opaque)
{
    gamepad_state *s = (gamepad_state *)opaque;
    int i;

    qemu_put_be32(f, s->extension);
    for (i = 0; i < s->num_buttons; i++)
        qemu_put_byte(f, s->buttons[i].pressed);
}

static int stellaris_gamepad_load(QEMUFile *f, void *opaque, int version_id)
{
    gamepad_state *s = (gamepad_state *)opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    s->extension = qemu_get_be32(f);
    for (i = 0; i < s->num_buttons; i++)
        s->buttons[i].pressed = qemu_get_byte(f);

    return 0;
}

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
    register_savevm(NULL, "stellaris_gamepad", -1, 1,
                    stellaris_gamepad_save, stellaris_gamepad_load, s);
}
