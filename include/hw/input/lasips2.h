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
 * + Named GPIO input "lasips2-port-input-irq[0..1]": set to 1 if the downstream
 *   LASIPS2Port has asserted its irq
 */

#ifndef HW_INPUT_LASIPS2_H
#define HW_INPUT_LASIPS2_H

#include "exec/hwaddr.h"
#include "hw/sysbus.h"
#include "hw/input/ps2.h"

#define TYPE_LASIPS2_PORT "lasips2-port"
OBJECT_DECLARE_TYPE(LASIPS2Port, LASIPS2PortDeviceClass, LASIPS2_PORT)

struct LASIPS2PortDeviceClass {
    DeviceClass parent;

    DeviceRealize parent_realize;
};

typedef struct LASIPS2State LASIPS2State;

struct LASIPS2Port {
    DeviceState parent_obj;

    LASIPS2State *lasips2;
    MemoryRegion reg;
    PS2State *ps2dev;
    uint8_t id;
    uint8_t control;
    uint8_t buf;
    bool loopback_rbne;
    qemu_irq irq;
};

#define TYPE_LASIPS2_KBD_PORT "lasips2-kbd-port"
OBJECT_DECLARE_SIMPLE_TYPE(LASIPS2KbdPort, LASIPS2_KBD_PORT)

struct LASIPS2KbdPort {
    LASIPS2Port parent_obj;

    PS2KbdState kbd;
};

#define TYPE_LASIPS2_MOUSE_PORT "lasips2-mouse-port"
OBJECT_DECLARE_SIMPLE_TYPE(LASIPS2MousePort, LASIPS2_MOUSE_PORT)

struct LASIPS2MousePort {
    LASIPS2Port parent_obj;

    PS2MouseState mouse;
};

struct LASIPS2State {
    SysBusDevice parent_obj;

    LASIPS2KbdPort kbd_port;
    LASIPS2MousePort mouse_port;
    uint8_t int_status;
    qemu_irq irq;
};

#define TYPE_LASIPS2 "lasips2"
OBJECT_DECLARE_SIMPLE_TYPE(LASIPS2State, LASIPS2)

#endif /* HW_INPUT_LASIPS2_H */
