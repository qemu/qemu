/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/input/stellaris_gamepad.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "ui/console.h"

typedef struct {
    uint32_t num_buttons;
    int extension;
    qemu_irq *irqs;
    uint32_t *keycodes;
    uint8_t *pressed;
} StellarisGamepad;

static void stellaris_gamepad_put_key(void * opaque, int keycode)
{
    StellarisGamepad *s = (StellarisGamepad *)opaque;
    int i;
    int down;

    if (keycode == 0xe0 && !s->extension) {
        s->extension = 0x80;
        return;
    }

    down = (keycode & 0x80) == 0;
    keycode = (keycode & 0x7f) | s->extension;

    for (i = 0; i < s->num_buttons; i++) {
        if (s->keycodes[i] == keycode && s->pressed[i] != down) {
            s->pressed[i] = down;
            qemu_set_irq(s->irqs[i], down);
        }
    }

    s->extension = 0;
}

static const VMStateDescription vmstate_stellaris_gamepad = {
    .name = "stellaris_gamepad",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(extension, StellarisGamepad),
        VMSTATE_VARRAY_UINT32(pressed, StellarisGamepad, num_buttons,
                              0, vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

/* Returns an array of 5 output slots.  */
void stellaris_gamepad_init(int n, qemu_irq *irq, const int *keycode)
{
    StellarisGamepad *s;
    int i;

    s = g_new0(StellarisGamepad, 1);
    s->irqs = g_new0(qemu_irq, n);
    s->keycodes = g_new0(uint32_t, n);
    s->pressed = g_new0(uint8_t, n);
    for (i = 0; i < n; i++) {
        s->irqs[i] = irq[i];
        s->keycodes[i] = keycode[i];
    }
    s->num_buttons = n;
    qemu_add_kbd_event_handler(stellaris_gamepad_put_key, s);
    vmstate_register(NULL, VMSTATE_INSTANCE_ID_ANY,
                     &vmstate_stellaris_gamepad, s);
}
