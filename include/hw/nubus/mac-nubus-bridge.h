/*
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_NUBUS_MAC_NUBUS_BRIDGE_H
#define HW_NUBUS_MAC_NUBUS_BRIDGE_H

#include "hw/nubus/nubus.h"
#include "qom/object.h"

#define MAC_NUBUS_FIRST_SLOT 0x9
#define MAC_NUBUS_LAST_SLOT  0xe
#define MAC_NUBUS_SLOT_NB    (MAC_NUBUS_LAST_SLOT - MAC_NUBUS_FIRST_SLOT + 1)

#define TYPE_MAC_NUBUS_BRIDGE "mac-nubus-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(MacNubusBridge, MAC_NUBUS_BRIDGE)

struct MacNubusBridge {
    NubusBridge parent_obj;

    MemoryRegion super_slot_alias;
    MemoryRegion slot_alias;
};

#endif
