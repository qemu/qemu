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
#include "exec/address-spaces.h"

#define NUBUS_SUPER_SLOT_SIZE 0x10000000U
#define NUBUS_SUPER_SLOT_NB   0x9

#define NUBUS_SLOT_SIZE       0x01000000
#define NUBUS_SLOT_NB         0xF

#define NUBUS_FIRST_SLOT      0x9
#define NUBUS_LAST_SLOT       0xF

#define TYPE_NUBUS_DEVICE "nubus-device"
#define NUBUS_DEVICE(obj) \
     OBJECT_CHECK(NubusDevice, (obj), TYPE_NUBUS_DEVICE)

#define TYPE_NUBUS_BUS "nubus-bus"
#define NUBUS_BUS(obj) OBJECT_CHECK(NubusBus, (obj), TYPE_NUBUS_BUS)

#define TYPE_NUBUS_BRIDGE "nubus-bridge"
#define NUBUS_BRIDGE(obj) OBJECT_CHECK(NubusBridge, (obj), TYPE_NUBUS_BRIDGE)

typedef struct NubusBus {
    BusState qbus;

    MemoryRegion super_slot_io;
    MemoryRegion slot_io;

    int current_slot;
} NubusBus;

typedef struct NubusDevice {
    DeviceState qdev;

    int slot_nb;
    MemoryRegion slot_mem;

    /* Format Block */

    MemoryRegion fblock_io;

    uint32_t rom_length;
    uint32_t rom_crc;
    uint8_t rom_rev;
    uint8_t rom_format;
    uint8_t byte_lanes;
    int32_t directory_offset;

    /* ROM */

    MemoryRegion rom_io;
    const uint8_t *rom;
} NubusDevice;

void nubus_register_rom(NubusDevice *dev, const uint8_t *rom, uint32_t size,
                        int revision, int format, uint8_t byte_lanes);

#endif
