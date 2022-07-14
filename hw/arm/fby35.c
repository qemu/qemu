/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"

#define TYPE_FBY35 MACHINE_TYPE_NAME("fby35")
OBJECT_DECLARE_SIMPLE_TYPE(Fby35State, FBY35);

struct Fby35State {
    MachineState parent_obj;
};

static void fby35_init(MachineState *machine)
{
}

static void fby35_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Meta Platforms fby35";
    mc->init = fby35_init;
}

static const TypeInfo fby35_types[] = {
    {
        .name = MACHINE_TYPE_NAME("fby35"),
        .parent = TYPE_MACHINE,
        .class_init = fby35_class_init,
        .instance_size = sizeof(Fby35State),
    },
};

DEFINE_TYPES(fby35_types);
