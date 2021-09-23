/*
 * QEMU Nubus
 *
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/nubus/nubus.h"


static void nubus_bridge_init(Object *obj)
{
    NubusBridge *s = NUBUS_BRIDGE(obj);
    NubusBus *bus = &s->bus;

    qbus_init(bus, sizeof(s->bus), TYPE_NUBUS_BUS, DEVICE(s), NULL);

    qdev_init_gpio_out(DEVICE(s), bus->irqs, NUBUS_IRQS);
}

static Property nubus_bridge_properties[] = {
    DEFINE_PROP_UINT16("slot-available-mask", NubusBridge,
                       bus.slot_available_mask, 0xffff),
    DEFINE_PROP_END_OF_LIST()
};

static void nubus_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->fw_name = "nubus";
    device_class_set_props(dc, nubus_bridge_properties);
}

static const TypeInfo nubus_bridge_info = {
    .name          = TYPE_NUBUS_BRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = nubus_bridge_init,
    .instance_size = sizeof(NubusBridge),
    .class_init    = nubus_bridge_class_init,
};

static void nubus_register_types(void)
{
    type_register_static(&nubus_bridge_info);
}

type_init(nubus_register_types)
