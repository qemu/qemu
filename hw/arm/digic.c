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

#include "hw/arm/digic.h"

static void digic_init(Object *obj)
{
    DigicState *s = DIGIC(obj);

    object_initialize(&s->cpu, sizeof(s->cpu), "arm946-" TYPE_ARM_CPU);
    object_property_add_child(obj, "cpu", OBJECT(&s->cpu), NULL);
}

static void digic_realize(DeviceState *dev, Error **errp)
{
    DigicState *s = DIGIC(dev);
    Error *err = NULL;

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
}

static void digic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = digic_realize;
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
