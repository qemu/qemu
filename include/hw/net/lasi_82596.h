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

#define TYPE_LASI_82596 "lasi_82596"
#define SYSBUS_I82596(obj) \
    OBJECT_CHECK(SysBusI82596State, (obj), TYPE_LASI_82596)

typedef struct {
    SysBusDevice parent_obj;

    I82596State state;
    uint16_t last_val;
    int val_index:1;
} SysBusI82596State;

SysBusI82596State *lasi_82596_init(MemoryRegion *addr_space,
                                    hwaddr hpa, qemu_irq irq);

#endif
