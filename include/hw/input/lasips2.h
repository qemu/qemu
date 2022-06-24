/*
 * QEMU LASI PS/2 emulation
 *
 * Copyright (c) 2019 Sven Schnelle
 *
 */
#ifndef HW_INPUT_LASIPS2_H
#define HW_INPUT_LASIPS2_H

#include "exec/hwaddr.h"
#include "hw/sysbus.h"

struct LASIPS2State;
typedef struct LASIPS2Port {
    struct LASIPS2State *parent;
    MemoryRegion reg;
    void *dev;
    uint8_t id;
    uint8_t control;
    uint8_t buf;
    bool loopback_rbne;
    bool irq;
} LASIPS2Port;

struct LASIPS2State {
    SysBusDevice parent_obj;

    LASIPS2Port kbd;
    LASIPS2Port mouse;
    qemu_irq irq;
};

#define TYPE_LASIPS2 "lasips2"
OBJECT_DECLARE_SIMPLE_TYPE(LASIPS2State, LASIPS2)

LASIPS2State *lasips2_initfn(hwaddr base, qemu_irq irq);

#endif /* HW_INPUT_LASIPS2_H */
