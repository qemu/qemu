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

#include "exec/memory.h"
#include "hw/irq.h"
#include "ui/console.h"
#include "qemu/timer.h"
#include "qom/object.h"

typedef enum  {
    MACFB_DISPLAY_APPLE_21_COLOR = 0,
    MACFB_DISPLAY_APPLE_PORTRAIT = 1,
    MACFB_DISPLAY_APPLE_12_RGB = 2,
    MACFB_DISPLAY_APPLE_2PAGE_MONO = 3,
    MACFB_DISPLAY_NTSC_UNDERSCAN = 4,
    MACFB_DISPLAY_NTSC_OVERSCAN = 5,
    MACFB_DISPLAY_APPLE_12_MONO = 6,
    MACFB_DISPLAY_APPLE_13_RGB = 7,
    MACFB_DISPLAY_16_COLOR = 8,
    MACFB_DISPLAY_PAL1_UNDERSCAN = 9,
    MACFB_DISPLAY_PAL1_OVERSCAN = 10,
    MACFB_DISPLAY_PAL2_UNDERSCAN = 11,
    MACFB_DISPLAY_PAL2_OVERSCAN = 12,
    MACFB_DISPLAY_VGA = 13,
    MACFB_DISPLAY_SVGA = 14,
} MacfbDisplayType;

typedef struct MacFbMode {
    uint8_t type;
    uint8_t depth;
    uint32_t mode_ctrl1;
    uint32_t mode_ctrl2;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t offset;
} MacFbMode;

#define MACFB_CTRL_TOPADDR  0x200
#define MACFB_NUM_REGS      (MACFB_CTRL_TOPADDR / sizeof(uint32_t))

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
    uint8_t type;

    uint32_t regs[MACFB_NUM_REGS];
    MacFbMode *mode;

    QEMUTimer *vbl_timer;
    qemu_irq irq;
} MacfbState;

#define TYPE_MACFB "sysbus-macfb"
OBJECT_DECLARE_SIMPLE_TYPE(MacfbSysBusState, MACFB)

struct MacfbSysBusState {
    SysBusDevice busdev;

    MacfbState macfb;
};

#define TYPE_NUBUS_MACFB "nubus-macfb"
OBJECT_DECLARE_TYPE(MacfbNubusState, MacfbNubusDeviceClass, NUBUS_MACFB)

struct MacfbNubusDeviceClass {
    DeviceClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
};


struct MacfbNubusState {
    NubusDevice busdev;

    MacfbState macfb;
};

#endif
