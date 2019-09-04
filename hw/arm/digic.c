/*
 * QEMU model of the Canon DIGIC SoC.
 *
 * Copyright (C) 2013 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This model is based on reverse engineering efforts
 * made by CHDK (http://chdk.wikia.com) and
 * Magic Lantern (http://www.magiclantern.fm) projects
 * contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/digic.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"

#define DIGIC4_TIMER_BASE(n)    (0xc0210000 + (n) * 0x100)

#define DIGIC_UART_BASE          0xc0800000

static void digic_init(Object *obj)
{
    DigicState *s = DIGIC(obj);
    int i;

    object_initialize_child(obj, "cpu", &s->cpu, sizeof(s->cpu),
                            ARM_CPU_TYPE_NAME("arm946"),
                            &error_abort, NULL);

    for (i = 0; i < DIGIC4_NB_TIMERS; i++) {
#define DIGIC_TIMER_NAME_MLEN    11
        char name[DIGIC_TIMER_NAME_MLEN];

        snprintf(name, DIGIC_TIMER_NAME_MLEN, "timer[%d]", i);
        sysbus_init_child_obj(obj, name, &s->timer[i], sizeof(s->timer[i]),
                              TYPE_DIGIC_TIMER);
    }

    sysbus_init_child_obj(obj, "uart", &s->uart, sizeof(s->uart),
                          TYPE_DIGIC_UART);
}

static void digic_realize(DeviceState *dev, Error **errp)
{
    DigicState *s = DIGIC(dev);
    Error *err = NULL;
    SysBusDevice *sbd;
    int i;

    object_property_set_bool(OBJECT(&s->cpu), true, "reset-hivecs", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    for (i = 0; i < DIGIC4_NB_TIMERS; i++) {
        object_property_set_bool(OBJECT(&s->timer[i]), true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        sbd = SYS_BUS_DEVICE(&s->timer[i]);
        sysbus_mmio_map(sbd, 0, DIGIC4_TIMER_BASE(i));
    }

    qdev_prop_set_chr(DEVICE(&s->uart), "chardev", serial_hd(0));
    object_property_set_bool(OBJECT(&s->uart), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    sbd = SYS_BUS_DEVICE(&s->uart);
    sysbus_mmio_map(sbd, 0, DIGIC_UART_BASE);
}

static void digic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = digic_realize;
    /* Reason: Uses serial_hds in the realize function --> not usable twice */
    dc->user_creatable = false;
}

static const TypeInfo digic_type_info = {
    .name = TYPE_DIGIC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(DigicState),
    .instance_init = digic_init,
    .class_init = digic_class_init,
};

static void digic_register_types(void)
{
    type_register_static(&digic_type_info);
}

type_init(digic_register_types)
