/*
 * Hexagon HMX Extension Context QOM Object
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/hexagon/hexagon_hmx_context.h"

static void hexagon_hmx_context_reset_hold(Object *obj, ResetType type)
{
    HexagonHMXContextState *s = HEXAGON_HMX_CONTEXT(obj);

    memset(&s->regs, 0, sizeof(s->regs));
}

static void hexagon_hmx_context_class_init(ObjectClass *klass,
                                           const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = hexagon_hmx_context_reset_hold;
    dc->user_creatable = false;
}

static const TypeInfo hexagon_hmx_context_info = {
    .name = TYPE_HEXAGON_HMX_CONTEXT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexagonHMXContextState),
    .class_init = hexagon_hmx_context_class_init,
};

static void hexagon_hmx_context_register_types(void)
{
    type_register_static(&hexagon_hmx_context_info);
}

type_init(hexagon_hmx_context_register_types)
