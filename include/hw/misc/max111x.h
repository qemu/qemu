/*
 * Maxim MAX1110/1111 ADC chip emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPLv2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_MISC_MAX111X_H
#define HW_MISC_MAX111X_H

#include "hw/ssi/ssi.h"
#include "qom/object.h"

/*
 * This is a model of the Maxim MAX1110/1111 ADC chip, which for QEMU
 * is an SSI slave device. It has either 4 (max1110) or 8 (max1111)
 * 8-bit ADC channels.
 *
 * QEMU interface:
 *  + GPIO inputs 0..3 (for max1110) or 0..7 (for max1111): set the value
 *    of each ADC input, as an unsigned 8-bit value
 *  + GPIO output 0: interrupt line
 *  + Properties "input0" to "input3" (max1110) or "input0" to "input7"
 *    (max1111): initial reset values for ADC inputs.
 *
 * Known bugs:
 *  + the interrupt line is not correctly implemented, and will never
 *    be lowered once it has been asserted.
 */
struct MAX111xState {
    SSIPeripheral parent_obj;

    qemu_irq interrupt;
    /* Values of inputs at system reset (settable by QOM property) */
    uint8_t reset_input[8];

    uint8_t tb1, rb2, rb3;
    int cycle;

    uint8_t input[8];
    int inputs, com;
};

#define TYPE_MAX_111X "max111x"

OBJECT_DECLARE_SIMPLE_TYPE(MAX111xState, MAX_111X)

#define TYPE_MAX_1110 "max1110"
#define TYPE_MAX_1111 "max1111"

#endif
