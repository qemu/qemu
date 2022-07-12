/*
 * QEMU LASI PS/2 emulation
 *
 * Copyright (c) 2019 Sven Schnelle
 *
 */

/*
 * QEMU interface:
 * + sysbus MMIO region 0: MemoryRegion defining the LASI PS2 keyboard
 *   registers
 * + sysbus MMIO region 1: MemoryRegion defining the LASI PS2 mouse
 *   registers
 * + sysbus IRQ 0: LASI PS2 output irq
 * + Named GPIO input "ps2-kbd-input-irq": set to 1 if the downstream PS2
 *   keyboard device has asserted its irq
 * + Named GPIO input "ps2-mouse-input-irq": set to 1 if the downstream PS2
 *   mouse device has asserted its irq
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

#endif /* HW_INPUT_LASIPS2_H */
