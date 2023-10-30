/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/input/stellaris_gamepad.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "ui/console.h"

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

static void stellaris_gamepad_realize(DeviceState *dev, Error **errp)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(dev);

    if (s->num_buttons == 0) {
        error_setg(errp, "keycodes property array must be set");
        return;
    }

    s->irqs = g_new0(qemu_irq, s->num_buttons);
    s->pressed = g_new0(uint8_t, s->num_buttons);
    qdev_init_gpio_out(dev, s->irqs, s->num_buttons);
    qemu_add_kbd_event_handler(stellaris_gamepad_put_key, dev);
}

static void stellaris_gamepad_reset_enter(Object *obj, ResetType type)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(obj);

    memset(s->pressed, 0, s->num_buttons * sizeof(uint8_t));
}

static Property stellaris_gamepad_properties[] = {
    DEFINE_PROP_ARRAY("keycodes", StellarisGamepad, num_buttons,
                      keycodes, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_END_OF_LIST(),
};

static void stellaris_gamepad_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = stellaris_gamepad_reset_enter;
    dc->realize = stellaris_gamepad_realize;
    dc->vmsd = &vmstate_stellaris_gamepad;
    device_class_set_props(dc, stellaris_gamepad_properties);
}

static const TypeInfo stellaris_gamepad_info[] = {
    {
        .name = TYPE_STELLARIS_GAMEPAD,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(StellarisGamepad),
        .class_init = stellaris_gamepad_class_init,
    },
};

DEFINE_TYPES(stellaris_gamepad_info);
