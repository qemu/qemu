/*
 * VMState interface
 *
 * Copyright (c) 2009-2019 Red Hat Inc
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vmstate-if.h"

static const TypeInfo vmstate_if_info = {
    .name = TYPE_VMSTATE_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(VMStateIfClass),
};

static void vmstate_register_types(void)
{
    type_register_static(&vmstate_if_info);
}

type_init(vmstate_register_types);
