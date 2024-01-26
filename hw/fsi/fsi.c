/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#include "qemu/osdep.h"

#include "hw/fsi/fsi.h"

static const TypeInfo fsi_bus_info = {
    .name = TYPE_FSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(FSIBus),
};

static void fsi_bus_register_types(void)
{
    type_register_static(&fsi_bus_info);
}

type_init(fsi_bus_register_types);
