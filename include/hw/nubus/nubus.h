/*
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_NUBUS_NUBUS_H
#define HW_NUBUS_NUBUS_H

#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "system/address-spaces.h"
#include "qom/object.h"
#include "qemu/units.h"

#define NUBUS_SUPER_SLOT_SIZE 0x10000000U
#define NUBUS_SUPER_SLOT_NB   0xe

#define NUBUS_SLOT_BASE       (NUBUS_SUPER_SLOT_SIZE * \
                               (NUBUS_SUPER_SLOT_NB + 1))

#define NUBUS_SLOT_SIZE       0x01000000
#define NUBUS_FIRST_SLOT      0x0
#define NUBUS_LAST_SLOT       0xf
#define NUBUS_SLOT_NB         (NUBUS_LAST_SLOT - NUBUS_FIRST_SLOT + 1)

#define NUBUS_IRQS            16

#define TYPE_NUBUS_DEVICE "nubus-device"
OBJECT_DECLARE_SIMPLE_TYPE(NubusDevice, NUBUS_DEVICE)

#define TYPE_NUBUS_BUS "nubus-bus"
OBJECT_DECLARE_SIMPLE_TYPE(NubusBus, NUBUS_BUS)

#define TYPE_NUBUS_BRIDGE "nubus-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(NubusBridge, NUBUS_BRIDGE);

struct NubusBus {
    BusState qbus;

    AddressSpace nubus_as;
    MemoryRegion nubus_mr;

    MemoryRegion super_slot_io;
    MemoryRegion slot_io;

    uint16_t slot_available_mask;

    qemu_irq irqs[NUBUS_IRQS];
};

#define NUBUS_DECL_ROM_MAX_SIZE    (1 * MiB)

struct NubusDevice {
    DeviceState qdev;

    int32_t slot;
    MemoryRegion super_slot_mem;
    MemoryRegion slot_mem;

    char *romfile;
    MemoryRegion decl_rom;
};

void nubus_set_irq(NubusDevice *nd, int level);

struct NubusBridge {
    SysBusDevice parent_obj;

    NubusBus bus;
};

#endif
