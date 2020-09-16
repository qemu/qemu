/*
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_NUBUS_MAC_H
#define HW_NUBUS_MAC_H

#include "hw/nubus/nubus.h"
#include "qom/object.h"

#define TYPE_MAC_NUBUS_BRIDGE "mac-nubus-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(MacNubusState, MAC_NUBUS_BRIDGE)

struct MacNubusState {
    SysBusDevice sysbus_dev;

    NubusBus *bus;
};

#endif
