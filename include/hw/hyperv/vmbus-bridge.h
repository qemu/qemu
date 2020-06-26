/*
 * QEMU Hyper-V VMBus root bridge
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_VMBUS_BRIDGE_H
#define HW_HYPERV_VMBUS_BRIDGE_H

#include "hw/sysbus.h"

#define TYPE_VMBUS_BRIDGE "vmbus-bridge"

typedef struct VMBus VMBus;

typedef struct VMBusBridge {
    SysBusDevice parent_obj;

    uint8_t irq;

    VMBus *bus;
} VMBusBridge;

#define VMBUS_BRIDGE(obj) OBJECT_CHECK(VMBusBridge, (obj), TYPE_VMBUS_BRIDGE)

static inline VMBusBridge *vmbus_bridge_find(void)
{
    return VMBUS_BRIDGE(object_resolve_path_type("", TYPE_VMBUS_BRIDGE, NULL));
}

#endif
