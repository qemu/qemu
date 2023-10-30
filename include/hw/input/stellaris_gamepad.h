/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_INPUT_STELLARIS_GAMEPAD_H
#define HW_INPUT_STELLARIS_GAMEPAD_H

#include "hw/sysbus.h"
#include "qom/object.h"

/*
 * QEMU interface:
 *  + QOM array property "keycodes": uint32_t QEMU keycodes to handle
 *    (these are QCodes, ie the Q_KEY_* values)
 *  + unnamed GPIO outputs: one per keycode, in the same order as the
 *    "keycodes" array property entries; asserted when key is down
 */

#define TYPE_STELLARIS_GAMEPAD "stellaris-gamepad"
OBJECT_DECLARE_SIMPLE_TYPE(StellarisGamepad, STELLARIS_GAMEPAD)

struct StellarisGamepad {
    SysBusDevice parent_obj;

    uint32_t num_buttons;
    qemu_irq *irqs;
    uint32_t *keycodes;
    uint8_t *pressed;
};

#endif
