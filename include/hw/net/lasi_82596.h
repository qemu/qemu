/*
 * QEMU LASI i82596 device emulation
 *
 * Copyright (c) 201 Helge Deller <deller@gmx.de>
 *
 */

#ifndef LASI_82596_H
#define LASI_82596_H

#include "net/net.h"
#include "hw/net/i82596.h"
#include "hw/sysbus.h"

#define TYPE_LASI_82596 "lasi_82596"
typedef struct SysBusI82596State SysBusI82596State;
DECLARE_INSTANCE_CHECKER(SysBusI82596State, SYSBUS_I82596,
                         TYPE_LASI_82596)

struct SysBusI82596State {
    SysBusDevice parent_obj;

    I82596State state;
    uint16_t last_val;
    int val_index:1;
};

SysBusI82596State *lasi_82596_init(MemoryRegion *addr_space, hwaddr hpa,
                                   qemu_irq irq, gboolean match_default);

#endif
