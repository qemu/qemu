/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Local bus where FSI slaves are connected
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/fsi/lbus.h"

#include "hw/qdev-properties.h"

#include "trace.h"

static void fsi_lbus_init(Object *o)
{
    FSILBus *lbus = FSI_LBUS(o);

    memory_region_init(&lbus->mr, OBJECT(lbus), TYPE_FSI_LBUS, 1 * MiB);
}

static const TypeInfo fsi_lbus_info = {
    .name = TYPE_FSI_LBUS,
    .parent = TYPE_BUS,
    .instance_init = fsi_lbus_init,
    .instance_size = sizeof(FSILBus),
};

static const TypeInfo fsi_lbus_device_type_info = {
    .name = TYPE_FSI_LBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FSILBusDevice),
    .abstract = true,
};

static void fsi_lbus_register_types(void)
{
    type_register_static(&fsi_lbus_info);
    type_register_static(&fsi_lbus_device_type_info);
}

type_init(fsi_lbus_register_types);
