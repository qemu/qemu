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

static void stellaris_gamepad_event(DeviceState *dev, QemuConsole *src,
                                    InputEvent *evt)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(dev);
    InputKeyEvent *key = evt->u.key.data;
    int qcode = qemu_input_key_value_to_qcode(key->key);
    int i;

    for (i = 0; i < s->num_buttons; i++) {
        if (s->keycodes[i] == qcode && s->pressed[i] != key->down) {
            s->pressed[i] = key->down;
            qemu_set_irq(s->irqs[i], key->down);
        }
    }
}

static const VMStateDescription vmstate_stellaris_gamepad = {
    .name = "stellaris_gamepad",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(pressed, StellarisGamepad, num_buttons,
                              0, vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static const QemuInputHandler stellaris_gamepad_handler = {
    .name = "Stellaris Gamepad",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = stellaris_gamepad_event,
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
    qemu_input_handler_register(dev, &stellaris_gamepad_handler);
}

static void stellaris_gamepad_finalize(Object *obj)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(obj);

    g_free(s->keycodes);
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
        .instance_finalize = stellaris_gamepad_finalize,
        .class_init = stellaris_gamepad_class_init,
    },
};

DEFINE_TYPES(stellaris_gamepad_info);
