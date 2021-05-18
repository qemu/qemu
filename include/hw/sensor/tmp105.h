/*
 * Texas Instruments TMP105 Temperature Sensor
 *
 * Browse the data sheet:
 *
 *    http://www.ti.com/lit/gpn/tmp105
 *
 * Copyright (C) 2012 Alex Horn <alex.horn@cs.ox.ac.uk>
 * Copyright (C) 2008-2012 Andrzej Zaborowski <balrogg@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#ifndef QEMU_TMP105_H
#define QEMU_TMP105_H

#include "hw/i2c/i2c.h"
#include "hw/sensor/tmp105_regs.h"
#include "qom/object.h"

#define TYPE_TMP105 "tmp105"
OBJECT_DECLARE_SIMPLE_TYPE(TMP105State, TMP105)

/**
 * TMP105State:
 * @config: Bits 5 and 6 (value 32 and 64) determine the precision of the
 * temperature. See Table 8 in the data sheet.
 *
 * @see_also: http://www.ti.com/lit/gpn/tmp105
 */
struct TMP105State {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/

    uint8_t len;
    uint8_t buf[2];
    qemu_irq pin;

    uint8_t pointer;
    uint8_t config;
    int16_t temperature;
    int16_t limit[2];
    int faults;
    uint8_t alarm;
    /*
     * The TMP105 initially looks for a temperature rising above T_high;
     * once this is detected, the condition it looks for next is the
     * temperature falling below T_low. This flag is false when initially
     * looking for T_high, true when looking for T_low.
     */
    bool detect_falling;
};

#endif
