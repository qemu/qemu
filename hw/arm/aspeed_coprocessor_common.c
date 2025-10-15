/*
 * ASPEED Coprocessor
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/memory.h"
#include "hw/qdev-properties.h"
#include "hw/arm/aspeed_coprocessor.h"

static void aspeed_coprocessor_realize(DeviceState *dev, Error **errp)
{
    AspeedCoprocessorState *s = ASPEED_COPROCESSOR(dev);

    if (!s->memory) {
        error_setg(errp, "'memory' link is not set");
        return;
    }
}

static const Property aspeed_coprocessor_properties[] = {
    DEFINE_PROP_LINK("memory", AspeedCoprocessorState, memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_LINK("sram", AspeedCoprocessorState, sram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("scu", AspeedCoprocessorState, scu, TYPE_ASPEED_SCU,
                     AspeedSCUState *),
    DEFINE_PROP_LINK("uart", AspeedCoprocessorState, uart, TYPE_SERIAL_MM,
                     SerialMM *),
    DEFINE_PROP_INT32("uart-dev", AspeedCoprocessorState, uart_dev, 0),
};

static void aspeed_coprocessor_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aspeed_coprocessor_realize;
    device_class_set_props(dc, aspeed_coprocessor_properties);
}

static const TypeInfo aspeed_coprocessor_types[] = {
    {
        .name           = TYPE_ASPEED_COPROCESSOR,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(AspeedCoprocessorState),
        .class_size     = sizeof(AspeedCoprocessorClass),
        .class_init     = aspeed_coprocessor_class_init,
        .abstract       = true,
    },
};

DEFINE_TYPES(aspeed_coprocessor_types)
