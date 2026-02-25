#ifndef MOCK_I3C_TARGET_H_
#define MOCK_I3C_TARGET_H_

/*
 * Mock I3C Device
 *
 * Copyright (c) 2025 Google LLC
 *
 * The mock I3C device can be thought of as a simple EEPROM. It has a buffer,
 * and the pointer in the buffer is reset to 0 on an I3C STOP.
 * To write to the buffer, issue a private write and send data.
 * To read from the buffer, issue a private read.
 *
 * The mock target also supports sending target interrupt IBIs.
 * To issue an IBI, set the 'ibi-magic-num' property to a non-zero number, and
 * send that number in a private transaction. The mock target will issue an IBI
 * after 1 second.
 *
 * It also supports a handful of CCCs that are typically used when probing I3C
 * devices.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/i3c/i3c.h"

#define TYPE_MOCK_I3C_TARGET "mock-i3c-target"
OBJECT_DECLARE_SIMPLE_TYPE(MockI3cTargetState, MOCK_I3C_TARGET)

struct MockI3cTargetState {
    I3CTarget parent_obj;

    /* General device state */
    bool can_ibi;
    QEMUTimer qtimer;
    size_t p_buf;
    uint8_t *buf;

    /* For Handing CCCs. */
    bool in_ccc;
    I3CCCC curr_ccc;
    uint8_t ccc_byte_offset;

    struct {
        uint32_t buf_size;
        uint8_t ibi_magic;
    } cfg;
};

#endif
