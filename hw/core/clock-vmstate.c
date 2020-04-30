/*
 * Clock migration structure
 *
 * Copyright GreenSocs 2019-2020
 *
 * Authors:
 *  Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/clock.h"

const VMStateDescription vmstate_clock = {
    .name = "clock",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(period, Clock),
        VMSTATE_END_OF_LIST()
    }
};
