/*
 * QEMU Motorola 680x0 Macintosh Video Card Emulation
 *                 Copyright (c) 2012-2018 Laurent Vivier
 *
 * some parts from QEMU G364 framebuffer Emulator.
 *                 Copyright (c) 2007-2011 Herve Poussineau
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MACFB_H
#define MACFB_H

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "ui/console.h"

typedef struct MacfbState {
    MemoryRegion mem_vram;
    MemoryRegion mem_ctrl;
    QemuConsole *con;

    uint8_t *vram;
    uint32_t vram_bit_mask;
    uint32_t palette_current;
    uint8_t color_palette[256 * 3];
    uint32_t width, height; /* in pixels */
    uint8_t depth;
} MacfbState;

#define TYPE_MACFB "sysbus-macfb"
#define MACFB(obj) \
    OBJECT_CHECK(MacfbSysBusState, (obj), TYPE_MACFB)

typedef struct {
    SysBusDevice busdev;

    MacfbState macfb;
} MacfbSysBusState;

#define MACFB_NUBUS_DEVICE_CLASS(class) \
    OBJECT_CLASS_CHECK(MacfbNubusDeviceClass, (class), TYPE_NUBUS_MACFB)
#define MACFB_NUBUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MacfbNubusDeviceClass, (obj), TYPE_NUBUS_MACFB)

typedef struct MacfbNubusDeviceClass {
    DeviceClass parent_class;

    DeviceRealize parent_realize;
} MacfbNubusDeviceClass;

#define TYPE_NUBUS_MACFB "nubus-macfb"
#define NUBUS_MACFB(obj) \
    OBJECT_CHECK(MacfbNubusState, (obj), TYPE_NUBUS_MACFB)

typedef struct {
    NubusDevice busdev;

    MacfbState macfb;
} MacfbNubusState;

#endif
