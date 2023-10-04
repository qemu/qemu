/*
 * QEMU IOSB emulation
 *
 * Copyright (c) 2019 Laurent Vivier
 * Copyright (c) 2022 Mark Cave-Ayland
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MEM_IOSB_H
#define HW_MEM_IOSB_H

#define IOSB_REGS 7

struct IOSBState {
    SysBusDevice parent_obj;

    MemoryRegion mem_regs;
    uint32_t regs[IOSB_REGS];
};

#define TYPE_IOSB "IOSB"
OBJECT_DECLARE_SIMPLE_TYPE(IOSBState, IOSB);

#endif
