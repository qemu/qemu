/*
 * Canon DIGIC UART block declarations.
 *
 * Copyright (C) 2013 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef HW_CHAR_DIGIC_UART_H
#define HW_CHAR_DIGIC_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define TYPE_DIGIC_UART "digic-uart"
#define DIGIC_UART(obj) \
    OBJECT_CHECK(DigicUartState, (obj), TYPE_DIGIC_UART)

enum {
    R_TX = 0x00,
    R_RX,
    R_ST = (0x14 >> 2),
    R_MAX
};

typedef struct DigicUartState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion regs_region;
    CharBackend chr;

    uint32_t reg_rx;
    uint32_t reg_st;
} DigicUartState;

#endif /* HW_CHAR_DIGIC_UART_H */
